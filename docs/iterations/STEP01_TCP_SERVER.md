# TCP 소켓 서버

## STEP1 - socket() → bind() → listen() → accept() 표준 흐름 구현

## 목표
POSIX 소켓 API로 클라이언트 TCP 연결을 수락하는 서버를 구현한다.

## 작업 범위
- socket()으로 서버 소켓 생성
- setsockopt()으로 소켓 옵션 설정
- bind()로 주소/포트 연결
- listen()으로 연결 대기열 열기
- accept()로 연결 수락
- 소켓 오류 처리와 자원 정리

## 관련 모듈

| 파일 | 책임 |
|---|---|
| include/tcpserver.h | 서버 모듈 외부 인터페이스 |
| src/tcpserver.c | listen 소켓 생성, 연결 수락 |
| src/main.c | 시그널 정책, accept 루프 |

연결별 상태는 connection 모듈이 맡는다.

## 의존 관계

```text
main.c → tcpserver.c → connection.c
```

## 구현 기록

| 위치 | 동작 |
|---|---|
| `tcpserver_listen(port, backlog)` | `getaddrinfo(AI_PASSIVE)`로 후보를 만들고 후보마다 `socket` → `setsockopt(SO_REUSEADDR)` → `bind` → `listen`. 성공하면 fd, 전부 실패하면 -1. `freeaddrinfo`로 후보 해제 |
| `tcpserver_accept(listen_fd)` | `accept`로 client fd 반환 |
| `main` | SIGPIPE 무시, SIGINT/SIGTERM은 `sigaction`(SA_RESTART 끔)으로 종료 플래그만 세움. accept 루프에서 `connection_handle` 호출. 종료 시 listen fd 닫고 exit 0 |

## 테스트 기록
- make → exit 0 (-Wall -Wextra -Werror).
- ./webserver 후 nc -zv 127.0.0.1 8080 → "succeeded".
- kill -TERM → 종료 코드 0 (graceful shutdown).

## 확인할 항목
- [x] 지정한 포트에서 실행
- [x] TCP 연결 수락
- [x] 잘못된 포트/소켓 생성 실패 처리
- [x] 종료 시 소켓 정리

## 관련 결정
- D1 — [STEP01 결정](../decisions/STEP01_TCP_SERVER_DECISIONS.md)