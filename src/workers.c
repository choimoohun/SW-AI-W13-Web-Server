#include "workers.h"
#include "io.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { WORKER_COUNT = 4, QUEUE_CAPACITY = 8 };

struct work_queue {
    pthread_mutex_t mutex;
    pthread_cond_t available;
    const struct http_config *config;
    int fds[QUEUE_CAPACITY];
    size_t head;
    size_t count;
    bool stopping;
};

static volatile sig_atomic_t shutdown_write_fd = -1;

static void request_shutdown(int signal_number)
{
    (void)signal_number;
    int saved_errno = errno;
    const char byte = 1;
    int fd = (int)shutdown_write_fd;
    if (fd >= 0) {
        ssize_t result;
        do {
            result = write(fd, &byte, 1);
        } while (result < 0 && errno == EINTR);
    }
    errno = saved_errno;
}

static bool enqueue(struct work_queue *queue, int connfd)
{
    pthread_mutex_lock(&queue->mutex);
    bool has_room = queue->count < QUEUE_CAPACITY;
    if (has_room) {
        size_t tail = (queue->head + queue->count) % QUEUE_CAPACITY;
        queue->fds[tail] = connfd;
        queue->count++;
        pthread_cond_signal(&queue->available);
    }
    pthread_mutex_unlock(&queue->mutex);
    return has_room;
}

static void *run_worker(void *arg)
{
    struct work_queue *queue = arg;
    for (;;) {
        pthread_mutex_lock(&queue->mutex);
        while (!queue->stopping && queue->count == 0) {
            pthread_cond_wait(&queue->available, &queue->mutex);
        }
        if (queue->stopping) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        int connfd = queue->fds[queue->head];
        queue->head = (queue->head + 1) % QUEUE_CAPACITY;
        queue->count--;
        pthread_mutex_unlock(&queue->mutex);

        handle_http_client(connfd, queue->config);
        close(connfd);
    }
}

static void reject_overload(int connfd)
{
    static const char response[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Retry-After: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if (set_nonblocking(connfd) == 0) {
        // Consume already-buffered bytes to reduce resets when rejecting a request.
        char discard[4096];
        (void)recv(connfd, discard, sizeof discard, 0);
        (void)send(connfd, response, sizeof response - 1, 0);
        (void)shutdown(connfd, SHUT_WR);
    }
    close(connfd);
}

static void stop_workers(struct work_queue *queue, pthread_t *threads, size_t started)
{
    pthread_mutex_lock(&queue->mutex);
    queue->stopping = true;
    pthread_cond_broadcast(&queue->available);
    pthread_mutex_unlock(&queue->mutex);

    // Workers stop taking jobs after stopping is set, so the main thread owns these FDs.
    for (size_t i = 0; i < queue->count; i++) {
        close(queue->fds[(queue->head + i) % QUEUE_CAPACITY]);
    }
    for (size_t i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }
}

static int accept_connections(int listenfd, int shutdown_fd, struct work_queue *queue)
{
    struct pollfd descriptors[] = {
        { .fd = listenfd, .events = POLLIN },
        { .fd = shutdown_fd, .events = POLLIN },
    };
    for (;;) {
        if (poll(descriptors, 2, -1) == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            return EXIT_FAILURE;
        }
        if (descriptors[1].revents != 0) return EXIT_SUCCESS;
        if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "Listening socket became unavailable\n");
            return EXIT_FAILURE;
        }
        int connfd = accept(listenfd, NULL, NULL);
        if (connfd == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == ECONNABORTED) continue;
            perror("accept");
            return EXIT_FAILURE;
        }
        if (!enqueue(queue, connfd)) reject_overload(connfd);
    }
}

static int start_workers(int listenfd, const struct http_config *config, const sigset_t *signals,
                         int shutdown_fd)
{
    struct work_queue queue = { .config = config };
    int error = pthread_mutex_init(&queue.mutex, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(error));
        close(listenfd);
        return EXIT_FAILURE;
    }
    error = pthread_cond_init(&queue.available, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(error));
        pthread_mutex_destroy(&queue.mutex);
        close(listenfd);
        return EXIT_FAILURE;
    }

    pthread_t threads[WORKER_COUNT];
    size_t worker_count = config->booking_backend ? 1 : WORKER_COUNT;
    size_t started = 0;
    int result = EXIT_FAILURE;
    while (started < worker_count) {
        error = pthread_create(&threads[started], NULL, run_worker, &queue);
        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            break;
        }
        started++;
    }
    if (started == worker_count) {
        // Only the accept thread unmasks the signals inherited by the workers.
        error = pthread_sigmask(SIG_UNBLOCK, signals, NULL);
        if (error == 0) {
            result = accept_connections(listenfd, shutdown_fd, &queue);
        } else {
            fprintf(stderr, "pthread_sigmask: %s\n", strerror(error));
        }
    }

    // Stop listening before waiting for active requests to finish.
    close(listenfd);
    stop_workers(&queue, threads, started);
    pthread_cond_destroy(&queue.available);
    pthread_mutex_destroy(&queue.mutex);
    return result;
}

int serve_connections(int listenfd, const struct http_config *config)
{
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    int error = pthread_sigmask(SIG_BLOCK, &signals, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_sigmask: %s\n", strerror(error));
        close(listenfd);
        return EXIT_FAILURE;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        close(listenfd);
        return EXIT_FAILURE;
    }
    int result = EXIT_FAILURE;
    if (set_nonblocking(pipe_fds[1]) == -1 || set_nonblocking(listenfd) == -1) {
        perror("fcntl");
    } else {
        struct sigaction action = { .sa_handler = request_shutdown };
        sigemptyset(&action.sa_mask);
        shutdown_write_fd = pipe_fds[1];
        if (sigaction(SIGINT, &action, NULL) == -1 || sigaction(SIGTERM, &action, NULL) == -1) {
            perror("sigaction");
        } else {
            result = start_workers(listenfd, config, &signals, pipe_fds[0]);
            listenfd = -1; // start_workers() closed the listening socket.
        }
    }
    // Workers are joined. Block signals before invalidating the handler's pipe FD.
    pthread_sigmask(SIG_BLOCK, &signals, NULL);
    shutdown_write_fd = -1;
    close(pipe_fds[1]);
    close(pipe_fds[0]);
    if (listenfd >= 0) close(listenfd);
    return result;
}
