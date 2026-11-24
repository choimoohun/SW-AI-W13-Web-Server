#ifndef TCPSERVER_H
#define TCPSERVER_H

/*
 * tcpserver — TCP 수신 소켓 생성과 연결 수락만 담당하는 모듈의 외부 인터페이스.
 *
 * [오늘 범위] FR-01. HTTP 문법/파일 경로는 여기서 다루지 않는다.
 *
 * [중요 — 이 헤더가 고쳐진 이유]
 * 예전에는 <sys/socket.h>/<netinet/in.h>가 이미 정의하는 struct sockaddr_in /
 * struct sockaddr를 여기에 다시 정의해서 컴파일 오류(재정의)가 났다.
 * 시스템 구조체/함수는 시스템 헤더가 선언하므로, 모듈 헤더는 '자기 API'만 선언한다.
 * (getaddrinfo/getnameinfo 시그니처를 여기에 붙여넣을 필요도 없다 — <netdb.h>가 선언.)
 *
 * [동작 원리 — 발표용]
 * 1) getaddrinfo(NULL, port, &hints, &res): 문자열 포트를 OS가 쓸 주소 후보 목록으로.
 *    hints.ai_family=AF_INET(오늘은 IPv4), ai_socktype=SOCK_STREAM, ai_flags=AI_PASSIVE.
 * 2) 각 후보: socket() → setsockopt(SO_REUSEADDR) → bind() → listen().
 *    - SO_REUSEADDR: 종료 직후 같은 주소/포트를 다시 bind할 때 이전 소켓 상태 때문에
 *      실패하는 일부 상황을 허용한다(TIME_WAIT이 관련될 수 있음). 동작은 OS와 바인딩
 *      주소 조합에 따라 다르며 모든 Address already in use를 해결하는 보장은 아니다.
 *      (FR-01 AC에서 재시작을 빠르게 하는 데 도움을 주는 옵션)
 *    - listen(fd, backlog): 커널에 '접속 대기열'을 연다. backlog는 accept()로 꺼내기 전
 *      대기열 크기 힌트로 FR-01 AC는 최소 128을 요청한다. 완료된 연결은 큐에 쌓인다.
 *      실제 상한은 OS 정책에 따라 제한될 수 있다.
 * 3) accept(listen_fd, ...): 대기열에서 연결 하나를 꺼내 '새 client fd'를 준다.
 *    listen fd(문)와 client fd(손님)는 서로 다른 자원 — 닫는 시점/소유자가 다르다.
 *
 * [소유권 계약] listen fd는 tcpserver가 소유(종료 시 닫음). accept가 준 client fd는
 * connection 모듈로 넘기며, 그 뒤 닫기는 connection의 단일 정리 지점이 맡는다.
 *
 * [시그널] SIGINT/SIGTERM 핸들러는 종료 플래그(main.c의 atomic_int g_stop)만 세운다.
 * accept 루프가 플래그를 보고 빠져나오며, 핸들러 안에서 close/free 같은 복잡한 정리를 하지 않는다.
 *
 * 아래 선언은 제안 인터페이스다. 구현하며 시그니처를 조정해도 된다.
 */

#include <sys/types.h>
#include <sys/socket.h>

/* port 문자열로 listen 소켓을 만들어 fd 반환. 실패 시 -1. backlog는 128 이상.
 * TODO: getaddrinfo 후보 순회 → socket/SO_REUSEADDR/bind/listen → freeaddrinfo. */
int tcpserver_listen(const char *port, int backlog);

/* listen_fd에서 연결 하나를 수락해 client fd 반환. 실패/종료 시 -1.
 * TODO: accept, EINTR 재시도, 종료 플래그 확인. client fd 소유권은 connection으로 이전. */
int tcpserver_accept(int listen_fd);

#endif /* TCPSERVER_H */
