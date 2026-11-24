#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char **argv)
{
    const int backlog = 16;                       // 수락 대기열 크기 요청
    const char *bind_address = argc > 3 ? argv[3] : "127.0.0.1";
    const char *document_root = argc > 2 ? argv[2] : "public";
    const char *port_text = argc > 1 ? argv[1] : "8080";
    char *port_end;
    long port = strtol(port_text, &port_end, 10);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    if (argc > 4 || port_end == port_text || *port_end != '\0' ||
        port < 1 || port > 65535 || inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        fprintf(stderr, "Usage: %s [port [document-root [bind-address]]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    address.sin_port = htons((uint16_t)port);

    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {       // 끊긴 연결에 쓸 때 SIGPIPE 무시
        perror("signal");
        return EXIT_FAILURE;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // IPv4 TCP 소켓 FD 받기
    if (listenfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (bind(listenfd, (const struct sockaddr *)&address, sizeof address) == -1) { // 소켓에 IP와 포트 지정
        perror("bind");
        close(listenfd);
        return EXIT_FAILURE;
    }

    if (listen(listenfd, backlog) == -1) {           // 새 접속을 받을 수 있게 설정
        perror("listen");
        close(listenfd);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Listening on %s:%ld (root: %s)\n", bind_address, port, document_root);
    int connfd = accept(listenfd, NULL, NULL);      // 접속을 기다렸다가 통신용 FD 받기
    if(connfd == -1) {
        perror("accept");
        close(listenfd);
        return EXIT_FAILURE;
    }

    printf("Client connected: fd=%d\n", connfd);        // 클라이언트와 통신할 FD 출력

    int result = handle_http_client(connfd, document_root);
    close(connfd);                                 // 이번 클라이언트 연결 닫기
    close(listenfd);                               // 한 연결 실습 종료
    return result;
}
