/*
 * tcpserver.c — TCP 수신 소켓과 접속 수락만 담당. (오늘 범위: FR-01)
 * 원리/소유권/시그널 계약은 include/tcpserver.h 주석 참고.
 *
 * 흐름: getaddrinfo → (후보마다) socket → setsockopt → bind → listen → freeaddrinfo.
 * 확인: make → main에서 tcpserver_listen("8080",128) 호출 → nc -zv 127.0.0.1 8080.
 */

#include "tcpserver.h"
#include <netdb.h>      /* getaddrinfo, freeaddrinfo, struct addrinfo, AI_PASSIVE */
#include <string.h>     /* memset */
#include <unistd.h>     /* close */
#include <sys/socket.h> /* socket, bind, listen, setsockopt, SOL_SOCKET, SO_REUSEADDR */

int tcpserver_listen(const char *port, int backlog)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;   /* getaddrinfo가 주는 주소 후보 목록(연결리스트) */
    struct addrinfo *p;            /* 후보 순회용 포인터 */
    int listen_fd = -1;            /* 아직 소켓 없음 → -1 (성공 시 fd로 바뀜) */
    int opt = 1;                   /* SO_REUSEADDR에 넣을 값 */

    /* 1) hints: AI_PASSIVE + node=NULL → bind(수동 열기, 서버)용 주소.
     *    반대편(클라이언트, 프록시 단계)은 AI_PASSIVE 없이 node=대상호스트로 호출. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* 오늘은 IPv4 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */
    hints.ai_flags    = AI_PASSIVE;   /* 서버 bind용 */

    /* 2) 문자열 port → 주소 후보 목록. 실패하면 -1.
     *    성공(rc==0)이면 return하지 말고 아래 for문으로 진행해야 소켓이 만들어진다. */
    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) return -1;

    /* 3) 후보를 돌며 socket → SO_REUSEADDR → bind → listen. 실패하면 다음 후보, 성공하면 break. */
    for (p = res; p != NULL; p = p->ai_next) {
        int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        /* TIME_WAIT 재바인딩 허용 — FR-01 AC: 재시작 즉시 bind */
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(fd, p->ai_addr, p->ai_addrlen) < 0) { close(fd); continue; }

        /* backlog는 128 이상(FR-01 AC) */
        if (listen(fd, backlog) < 0) { close(fd); continue; }

        listen_fd = fd;
        break;
    }

    /* 4) 후보 목록 해제 — 누수 방지 NFR-01 */
    freeaddrinfo(res);
    return listen_fd;   /* 성공: listen fd, 모두 실패: -1 */
}

/* tcpserver_accept: 연결 하나를 수락해 client fd 반환. 실패 시 accept가 -1 반환.
 * 수락한 client fd는 connection으로 넘긴다(닫기 소유권 이전).
 * TODO(다음 단계): EINTR 재시도 + 종료 플래그 확인을 이 함수 안에서 정교화. */
int tcpserver_accept(int listen_fd)
{
    int client_fd = accept(listen_fd, NULL, NULL);
    return client_fd;
}
