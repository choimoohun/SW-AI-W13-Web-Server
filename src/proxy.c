/*
 * proxy.c — 리버스 프록시(FR-07).
 *  - 경로 prefix로 백엔드 선택(게이트웨이 역할)
 *  - 백엔드 2대 이상이면 Round-Robin(로드 밸런서 역할)
 *  - X-Forwarded-For 추가, 연결 실패 502, 응답 지연 504
 *  - 전용 스레드 + 큐로 워커 이벤트 루프를 블로킹하지 않음
 *  - C-02 준수: 외부 라이브러리 없이 POSIX 소켓만 사용
 */
#include "proxy.h"

#include "http_response.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define UPSTREAM_BUF   8192
#define PROXY_URI_CAP  2048
#define PROXY_BODY_CAP 8192
#define PROXY_QUEUE_MAX 256   /* 기본 상한. conf의 proxy_queue_max가 우선한다. */

typedef struct proxy_job {
    int client_fd;
    int route_index;
    int worker_id;
    const server_config_t *cfg;
    char method[16];
    size_t method_len;
    char uri[PROXY_URI_CAP];
    size_t uri_len;
    char body[PROXY_BODY_CAP];
    size_t body_len;
    long content_length;
    char client_ip[64];
    struct proxy_job *next;
} proxy_job_t;

static proxy_job_t *g_head;
static proxy_job_t *g_tail;
static int g_qcount;
static pthread_mutex_t g_qmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;
static volatile int g_stop;
static int g_started;
static _Atomic unsigned g_rr;

/* ---------- 소켓 유틸 ---------- */

/* client fd는 nonblocking일 수 있으므로 EAGAIN이면 writable까지 기다린다. */
static int send_client(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd p;
            p.fd = fd; p.events = POLLOUT; p.revents = 0;
            if (poll(&p, 1, 5000) <= 0) return -1;
            continue;
        }
        return -1;
    }
    return 0;
}

static void send_error(int fd, int status)
{
    char buf[512];
    size_t n = http_response_build_error(buf, sizeof(buf), status, 0);
    if (n > 0) (void)send_client(fd, buf, n);
}

static int connect_backend(const char *host, const char *port, int timeout_sec)
{
    struct addrinfo hints, *res = NULL, *p;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    for (p = res; p != NULL; p = p->ai_next) {
        int flags;
        int rc;
        struct pollfd pf;
        socklen_t len = sizeof(int);
        int soerr = 0;

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        flags = fcntl(fd, F_GETFL, 0);
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        rc = connect(fd, p->ai_addr, p->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            pf.fd = fd; pf.events = POLLOUT; pf.revents = 0;
            if (poll(&pf, 1, timeout_sec * 1000) <= 0) {
                close(fd); fd = -1; continue;
            }
            (void)getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
            if (soerr != 0) { close(fd); fd = -1; continue; }
        } else if (rc < 0) {
            close(fd); fd = -1; continue;
        }
        (void)fcntl(fd, F_SETFL, flags);   /* blocking 복귀 */
        {
            struct timeval tv;
            tv.tv_sec = timeout_sec; tv.tv_usec = 0;
            (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        break;
    }
    freeaddrinfo(res);
    return fd;
}

static int parse_status(const char *buf, ssize_t len)
{
    char tmp[16];
    ssize_t n = len < 12 ? len : 12;
    if (n < 12 || memcmp(buf, "HTTP/1.", 7) != 0) return 0;
    memcpy(tmp, buf + 9, n - 9);
    tmp[n - 9] = '\0';
    return (int)strtol(tmp, NULL, 10);
}

/* ---------- 접근 로그 (프록시 스레드 소유) ---------- */

static void proxy_log(const proxy_job_t *j, int status)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_REALTIME, &now);
    fprintf(stderr, "[%ld.%03ld] [w%d] [%s] \"%.*s %.*s\" %d 0 0ms\n",
            (long)now.tv_sec, now.tv_nsec / 1000000L, j->worker_id, j->client_ip,
            (int)j->method_len, j->method, (int)j->uri_len, j->uri, status);
}

/* ---------- 실제 중계 (프록시 스레드에서만 호출) ---------- */

static void process_job(proxy_job_t *j)
{
    const server_config_t *cfg = j->cfg;
    const cfg_route_t *r;
    char host[CFG_ADDR_LEN];
    char port[16];
    char reqbuf[16384];
    size_t used = 0;
    char buf[UPSTREAM_BUF];
    int backend;
    int first = 1;
    int status = 502;

    if (cfg == NULL || j->route_index < 0 || j->route_index >= cfg->route_count) {
        send_error(j->client_fd, 502);
        proxy_log(j, 502);
        return;
    }
    r = &cfg->routes[j->route_index];
    if (r->backend_count == 0) {
        send_error(j->client_fd, 502);
        proxy_log(j, 502);
        return;
    }

    {
        unsigned pick = atomic_fetch_add(&g_rr, 1);
        backend = (int)(pick % (unsigned)r->backend_count);
    }
    {
        const char *colon = strrchr(r->backends[backend], ':');
        if (colon == NULL) {
            send_error(j->client_fd, 502);
            proxy_log(j, 502);
            return;
        }
        size_t hlen = (size_t)(colon - r->backends[backend]);
        if (hlen >= sizeof(host)) {
            send_error(j->client_fd, 502);
            proxy_log(j, 502);
            return;
        }
        memcpy(host, r->backends[backend], hlen);
        host[hlen] = '\0';
        if (strlen(colon + 1) >= sizeof(port)) {
            send_error(j->client_fd, 502);
            proxy_log(j, 502);
            return;
        }
        strcpy(port, colon + 1);
    }

    backend = connect_backend(host, port, cfg->backend_timeout_sec);
    if (backend < 0) {
        send_error(j->client_fd, 502);
        proxy_log(j, 502);
        return;
    }

    used = (size_t)snprintf(reqbuf + used, sizeof(reqbuf) - used,
                            "%.*s %.*s HTTP/1.1\r\n"
                            "Host: %s:%s\r\n"
                            "X-Forwarded-For: %s\r\n"
                            "Connection: close\r\n",
                            (int)j->method_len, j->method,
                            (int)j->uri_len, j->uri, host, port,
                            j->client_ip[0] == '\0' ? "" : j->client_ip);
    if (j->content_length >= 0) {
        used += (size_t)snprintf(reqbuf + used, sizeof(reqbuf) - used,
                                 "Content-Length: %ld\r\n", j->content_length);
    }
    used += (size_t)snprintf(reqbuf + used, sizeof(reqbuf) - used, "\r\n");
    if (j->body_len > 0 && used + j->body_len < sizeof(reqbuf)) {
        memcpy(reqbuf + used, j->body, j->body_len);
        used += j->body_len;
    }
    if (send(backend, reqbuf, used, 0) < 0) {
        close(backend);
        send_error(j->client_fd, 502);
        proxy_log(j, 502);
        return;
    }

    for (;;) {
        ssize_t n = recv(backend, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(backend);
            if (first || errno == EAGAIN || errno == EWOULDBLOCK) {
                send_error(j->client_fd, 504);
                status = 504;
            }
            break;
        }
        if (n == 0) break;
        if (first) {
            status = parse_status(buf, n);
            first = 0;
        }
        if (send_client(j->client_fd, buf, (size_t)n) < 0) break;
    }
    close(backend);
    proxy_log(j, status);
}

/* ---------- 큐 / 스레드 ---------- */

static void *proxy_thread_main(void *arg)
{
    (void)arg;
    for (;;) {
        proxy_job_t *j;

        pthread_mutex_lock(&g_qmu);
        while (g_head == NULL && !g_stop) {
            pthread_cond_wait(&g_cv, &g_qmu);
        }
        if (g_head == NULL && g_stop) {
            pthread_mutex_unlock(&g_qmu);
            break;
        }
        j = g_head;
        g_head = j->next;
        if (g_head == NULL) g_tail = NULL;
        g_qcount--;
        pthread_mutex_unlock(&g_qmu);

        process_job(j);
        close(j->client_fd);
        free(j);
    }
    return NULL;
}

int proxy_start(void)
{
    g_stop = 0;
    if (g_started) return 0;
    if (pthread_create(&g_thread, NULL, proxy_thread_main, NULL) != 0) return -1;
    g_started = 1;
    return 0;
}

void proxy_stop(void)
{
    if (!g_started) return;
    pthread_mutex_lock(&g_qmu);
    g_stop = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_qmu);
    (void)pthread_join(g_thread, NULL);
    g_started = 0;
}

int proxy_submit(int client_fd, const http_request_t *req,
                 const server_config_t *cfg, int route_index,
                 const char *client_ip, int worker_id)
{
    proxy_job_t *j = calloc(1, sizeof(*j));

    if (j == NULL) return -1;

    j->client_fd = dup(client_fd);
    if (j->client_fd < 0) { free(j); return -1; }

    j->cfg = cfg;
    j->route_index = route_index;
    j->worker_id = worker_id;
    j->method_len = req->method_len < sizeof(j->method) - 1
                        ? req->method_len : sizeof(j->method) - 1;
    memcpy(j->method, req->method, j->method_len);
    j->method[j->method_len] = '\0';

    /* rewrite가 설정돼 있으면 prefix를 잘라내고 rewrite로 바꿔 백엔드에 전달. */
    j->uri_len = config_apply_rewrite(&cfg->routes[route_index], req->uri, req->uri_len,
                                      j->uri, sizeof(j->uri));
    j->uri[j->uri_len] = '\0';

    j->body_len = req->body_len < sizeof(j->body)
                      ? req->body_len : sizeof(j->body);
    if (j->body_len > 0 && req->body != NULL) {
        memcpy(j->body, req->body, j->body_len);
    }
    j->content_length = req->content_length;
    if (client_ip != NULL) {
        size_t n = strlen(client_ip);
        if (n >= sizeof(j->client_ip)) n = sizeof(j->client_ip) - 1;
        memcpy(j->client_ip, client_ip, n);
        j->client_ip[n] = '\0';
    }

    pthread_mutex_lock(&g_qmu);
    {
        int cap = (cfg->proxy_queue_max > 0) ? cfg->proxy_queue_max : PROXY_QUEUE_MAX;
        if (g_qcount >= cap) {
            pthread_mutex_unlock(&g_qmu);
            close(j->client_fd);
            free(j);
            return -2;   /* 큐 포화: 호출부에서 503 */
        }
    }
    if (g_tail == NULL) g_head = j;
    else g_tail->next = j;
    g_tail = j;
    g_qcount++;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_qmu);
    return 0;
}
