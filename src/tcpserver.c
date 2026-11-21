/*
 * tcpserver.c — TCP 수신 소켓과 접속 수락만 담당. (오늘 범위: FR-01)
 * 원리/소유권/시그널 계약은 include/tcpserver.h 주석 참고.
 *
 * 아래는 '골격 스캐폴딩'이다. 변수/힌트/반복문 구조는 채워져 있고,
 * 실제 시스템콜 호출과 오류 처리는 TODO 빈칸을 직접 채워 넣으세요.
 * 채우는 순서: getaddrinfo → socket → setsockopt → bind → listen → freeaddrinfo.
 * 다 채우면 make → main에서 tcpserver_listen("8080",128) 호출 → nc -zv 127.0.0.1 8080 으로 확인.
 * (TODO를 채우기 전엔 unused 변수 경고로 make가 실패할 수 있음 — 정상. 채우고 make.)
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
    (void)opt;                     /* TODO(setsockopt) 채우면 이 줄 제거 */

    /* 1) hints: 어떤 주소 후보를 받을지 지정. (채워짐 — 의미를 이해할 것)
     *    AI_PASSIVE + node=NULL → bind(수동 열기, 서버)용 주소.
     *    반대편(클라이언트, 프록시 단계)은 AI_PASSIVE 없이 node=대상호스트로 호출. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* 오늘은 IPv4 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */
    hints.ai_flags    = AI_PASSIVE;   /* 서버 bind용 */

    /* 2) TODO: 문자열 port → 주소 후보 목록.
     *    int rc = getaddrinfo(NULL, port, &hints, &res);
     *    rc != 0 이면 실패 → return -1;  (성공 시 res에 연결리스트가 담김) */

    /* 3) 후보를 돌며 socket → SO_REUSEADDR → bind → listen 시도.
     *    한 후보가 실패하면 다음 후보로(continue), 성공하면 break. */
    for (p = res; p != NULL; p = p->ai_next) {
        int fd;
        (void)fd;                  /* TODO(socket) 채우면 이 줄 제거 */

        /* TODO: fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
         *       fd < 0 이면 continue; */

        /* TODO: setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
         *       (TIME_WAIT 재바인딩 허용 — FR-01 AC: 재시작 즉시 bind) */

        /* TODO: if (bind(fd, p->ai_addr, p->ai_addrlen) < 0) { close(fd); continue; } */

        /* TODO: if (listen(fd, backlog) < 0) { close(fd); continue; }
         *       backlog는 128 이상(FR-01 AC). */

        /* TODO: 성공 → listen_fd = fd; break; */
    }

    /* 4) TODO: freeaddrinfo(res);  (후보 목록 해제 — 누수 방지 NFR-01) */

    return listen_fd;   /* 성공: listen fd, 모두 실패: -1 */
}

/* tcpserver_accept: listen_fd에서 연결 하나를 수락해 client fd 반환. (직접 구현)
 * 힌트: accept(listen_fd, NULL, NULL) → client fd.
 *       EINTR이면 재시도(종료 플래그 확인), 종료 플래그 시 -1.
 *       수락한 client fd는 connection으로 넘긴다(닫기 소유권 이전). */
int tcpserver_accept(int listen_fd)
{
    (void)listen_fd;   /* TODO: accept에서 사용 */
    /* TODO: 직접 구현 */
    return -1;
}
