#include "http.h"
#include "workers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

static bool parse_port(const char *text, uint16_t *port)
{
    if (*text == '\0' || text[strspn(text, "0123456789")] != '\0') return false;
    long value = strtol(text, NULL, 10);
    if (value < 1 || value > 65535) return false;
    *port = htons((uint16_t)value);
    return true;
}

static bool configure_mode(int argc, char **argv, struct http_config *config)
{
    if (argc <= 4) return true;
    if (argc == 5 && strcmp(argv[4], "--booking") == 0) {
        config->booking_backend = true;
        return true;
    }
    if (argc < 7 || argc > 8 || strcmp(argv[4], "--proxy") != 0) return false;
    const char *prefix = argc == 8 ? argv[7] : "/api/";
    size_t length = strlen(prefix);
    if (length < 2 || prefix[0] != '/' || prefix[length - 1] != '/' ||
        prefix[strspn(prefix, "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")] != '\0')
        return false;
    config->proxy_prefix = prefix;
    config->backend.sin_family = AF_INET;
    return parse_port(argv[6], &config->backend.sin_port) &&
           inet_pton(AF_INET, argv[5], &config->backend.sin_addr) == 1;
}

int main(int argc, char **argv)
{
    const int backlog = 16;                       // 수락 대기열 크기 요청
    const char *bind_address = argc > 3 ? argv[3] : "127.0.0.1";
    const char *port_text = argc > 1 ? argv[1] : "8080";
    struct http_config config = { .document_root = argc > 2 ? argv[2] : "public" };
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    if (!parse_port(port_text, &address.sin_port) ||
        inet_pton(AF_INET, bind_address, &address.sin_addr) != 1 ||
        !configure_mode(argc, argv, &config)) {
        fprintf(stderr, "Usage: %s [port [document-root [bind-address "
                        "[--booking | --proxy backend-ip backend-port [path-prefix]]]]]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {       // 끊긴 연결에 쓸 때 SIGPIPE 무시
        perror("signal");
        return EXIT_FAILURE;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // IPv4 TCP 소켓 FD 받기
    if (listenfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) == -1) {
        perror("setsockopt");
        close(listenfd);
        return EXIT_FAILURE;
    }
    if (bind(listenfd, (const struct sockaddr *)&address, sizeof address) == -1) {
        perror("bind");
        close(listenfd);
        return EXIT_FAILURE;
    }

    if (listen(listenfd, backlog) == -1) {           // 새 접속을 받을 수 있게 설정
        perror("listen");
        close(listenfd);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Listening on %s:%u (root: %s)\n", bind_address,
            (unsigned)ntohs(address.sin_port), config.document_root);
    if (config.booking_backend || config.proxy_prefix != NULL) {
        return serve_connections(listenfd, &config);
    }

    int connfd;
    do {
        connfd = accept(listenfd, NULL, NULL);  // 기본 실습 모드는 한 연결만 받는다.
    } while (connfd == -1 && errno == EINTR);
    if (connfd == -1) {
        perror("accept");
        close(listenfd);
        return EXIT_FAILURE;
    }
    int result = handle_http_client(connfd, &config);
    close(connfd);
    close(listenfd);
    return result;
}
