/*
 * httpd.c - 스레드 풀 웹 서버 (CS:APP3e 11.6 Tiny와 12.5.5 prethreaded 서버 기반)
 * 설계: docs/static-file-serving/design.md, docs/dynamic-request/design.md, docs/concurrency/design.md
 */
#include "cgi.h"
#include "conn_queue.h"
#include "http_request.h"
#include "http_response.h"
#include "static_file.h"

#define NWORKERS 8
#define CONN_TIMEOUT_SEC 5 /* docs/static-file-serving/design.md#연결-시간-제한 */
#define CGI_PREFIX "/cgi-bin/"

static conn_queue queue;
static const char *root;
static volatile sig_atomic_t stop;

static void handle_connection(int connfd)
{
    rio_t rio;
    http_request req = {0};
    char buf[MAXLINE], path[MAXLINE];
    size_t len;
    int rc;

    rio_readinitb(&rio, connfd);
    rc = http_read_head(&rio, buf, sizeof buf, &len);
    if (rc != HTTP_READ_OK) {
        if (rc == HTTP_READ_BAD)
            http_send_error(connfd, 400);
        goto out;
    }
    if ((rc = http_parse_head(buf, len, &req)) != 0) {
        http_send_error(connfd, rc);
        goto out;
    }
    printf("%s %s %s\n", req.method, req.uri, req.version);
    fflush(stdout);

    rc = http_read_body(&rio, &req);
    if (rc != HTTP_READ_OK) {
        if (rc == HTTP_READ_NOMEM)
            http_send_error(connfd, 500);
        goto out;
    }
    int dynamic = strncmp(req.uri, CGI_PREFIX, strlen(CGI_PREFIX)) == 0;
    int allowed = strcmp(req.method, "GET") == 0 || (dynamic && strcmp(req.method, "POST") == 0);
    if (!allowed) {
        http_send_error(connfd, 501);
        goto out;
    }
    if ((rc = static_resolve_path(root, req.uri, path, sizeof path)) != 0) {
        http_send_error(connfd, rc);
        goto out;
    }
    if (dynamic)
        cgi_serve(connfd, path, &req);
    else
        static_serve(connfd, path);
out:
    http_request_free(&req);
}

static void *worker(void *arg)
{
    (void)arg;
    int connfd;
    while ((connfd = conn_queue_pop(&queue)) >= 0) {
        handle_connection(connfd);
        close(connfd);
    }
    return NULL;
}

static void on_stop(int sig)
{
    (void)sig;
    stop = 1;
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    pthread_t tid[NWORKERS];
    sigset_t stop_signals;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <root>\n", argv[0]);
        exit(0);
    }
    root = argv[2];

    /* 끊긴 소켓에 쓸 때 프로세스가 죽지 않고 EPIPE를 받도록 합니다. */
    Signal(SIGPIPE, SIG_IGN);
    /* 교재 Signal()과 달리 SA_RESTART 없이 등록해야 막혀 있던 accept()가 EINTR로 깨어납니다. */
    struct sigaction sa = {.sa_handler = on_stop};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        unix_error("sigaction error");

    listenfd = Open_listenfd(argv[1]);
    fcntl(listenfd, F_SETFD, FD_CLOEXEC);
    if (conn_queue_init(&queue) < 0)
        app_error("conn_queue_init failed");

    /* 종료 신호가 accept()의 main에만 가도록, 막아 둔 채 워커를 만들어 막힌 상태를 물려줍니다. */
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &stop_signals, NULL);
    for (int i = 0; i < NWORKERS; i++)
        Pthread_create(&tid[i], NULL, worker, NULL);
    pthread_sigmask(SIG_UNBLOCK, &stop_signals, NULL);

    /* shortcut: stop 검사와 accept() 사이에 신호가 오면 다음 연결이 올 때까지, 대기열이 가득 차 push에서 기다리면
       워커가 자리를 낼 때까지 종료가 늦어짐. 앞의 경우는 신호를 한 번 더 보내면 끝남 */
    while (!stop) {
        connfd = accept(listenfd, NULL, NULL);
        if (connfd < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            unix_error("Accept error");
        }
        /* 워커가 fork한 CGI에 다른 손님의 연결이 새지 않게 합니다.
           shortcut: accept()와 fcntl() 사이에 워커가 fork하면 샘. 그 CGI가 끝날 때까지 FIN이 늦어질 뿐이라 둠 */
        fcntl(connfd, F_SETFD, FD_CLOEXEC);
        /* 멈춘 클라이언트가 워커를 붙잡지 않게 read/write마다 시간 제한을 겁니다.
           shortcut: 호출마다의 제한이라 바이트를 흘려 보내는 클라이언트는 워커 하나를 오래 잡음 */
        struct timeval tv = {.tv_sec = CONN_TIMEOUT_SEC};
        if (setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0 ||
            setsockopt(connfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0)
            fprintf(stderr, "setsockopt timeout: %s\n", strerror(errno));
        conn_queue_push(&queue, connfd);
    }

    /* 새 연결은 받지 않고, 이미 받은 연결은 워커가 모두 응답한 뒤 끝냅니다. */
    close(listenfd);
    conn_queue_close(&queue);
    for (int i = 0; i < NWORKERS; i++)
        Pthread_join(tid[i], NULL);
    conn_queue_destroy(&queue);
    return 0;
}
