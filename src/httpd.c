/*
 * httpd.c - 순차 정적 파일 웹 서버 (CS:APP3e 11.6 Tiny 기반)
 * 설계: docs/static-file-serving/design.md
 */
#include "http_request.h"
#include "http_response.h"
#include "static_file.h"

static void handle_connection(int connfd, const char *root)
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
    if (strcmp(req.method, "GET") != 0) {
        http_send_error(connfd, 501);
        goto out;
    }
    if ((rc = static_resolve_path(root, req.uri, path, sizeof path)) != 0) {
        http_send_error(connfd, rc);
        goto out;
    }
    static_serve(connfd, path);
out:
    http_request_free(&req);
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <root>\n", argv[0]);
        exit(0);
    }

    /* 끊긴 소켓에 쓸 때 프로세스가 죽지 않고 EPIPE를 받도록 합니다. */
    Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            unix_error("Accept error");
        }
        handle_connection(connfd, argv[2]);
        Close(connfd);
    }
}
