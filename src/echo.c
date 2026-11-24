/*
 * echo - read and echo text lines until client closes connection
 * (CS:APP3e Figure 11.22, 연결 오류 시 그 연결만 닫도록 보완)
 */
#include <errno.h>

#include "csapp.h"

void echo(int connfd)
{
    ssize_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    /* 래퍼는 오류 시 서버 전체를 끝내므로 원시 함수의 반환값으로 이 연결만 정리합니다. */
    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        printf("server received %d bytes\n", (int)n);
        if (rio_writen(connfd, buf, n) < 0) {
            fprintf(stderr, "rio_writen error: %s\n", strerror(errno));
            return;
        }
    }
    if (n < 0)
        fprintf(stderr, "rio_readlineb error: %s\n", strerror(errno));
}
