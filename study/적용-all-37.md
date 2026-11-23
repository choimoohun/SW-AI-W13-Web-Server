# 37개 학습 자료 중 이 프로젝트에 적용한 내용

원본: `study/all-37-materials-ultra-detailed.md`

이 문서는 그 자료에서 **실제로 SW-AI-W13-Web-Server에 반영된 것**만 골라 정리한 것입니다.
적용 여부는 세 가지로 나눕니다.

- **적용** — 코드나 문서에 직접 반영됨.
- **부분** — 개념은 썼지만 방식은 다르게 구현.
- **미적용** — 참고만 했거나 범위 밖(C-02 외부 라이브러리 금지, UDP 미사용 등).

## 전체 표

| # | 자료 | 적용 | 이 프로젝트에 들어간 모습 |
|---|---|---|---|
| 1 | BJ.19 block/non-block I/O, I/O multiplexing | 적용 | `O_NONBLOCK` + `iomux`(epoll/kqueue/poll) + 워커 이벤트 루프. 결정 D5/D9/D21 |
| 2 | Beej's Guide (socket API, `getaddrinfo`) | 적용 | `tcpserver.c`(socket/bind/listen/accept, `SO_REUSEADDR`), `proxy.c`(`getaddrinfo`), `connection.c`(`sockaddr_storage`) |
| 3 | UDP `connect()` | 미적용 | UDP를 쓰지 않음 |
| 4 | Endianness | 부분 | `htons`, `inet_pton` 등 네트워크 바이트 오더 처리만 |
| 5 | Checksum | 미적용 | TCP/커널이 이미 처리 |
| 6 | libcurl | 미적용 | C-02 외부 라이브러리 금지 |
| 7 | Code review(캡슐화, `snprintf`) | 적용 | `snprintf` 사용, 모듈 내부 함수는 `static` |
| 8 | select로 한 스레드가 여러 소켓 | 적용 | `iomux`의 `poll` 폴백이 같은 패턴 |
| 9 | UDP 송수신 | 미적용 | UDP를 쓰지 않음 |
| 10 | web client(sockets) | 부분 | `proxy.c`가 백엔드 클라이언트 역할 |
| 11 | `getaddrinfo`(DNS) | 적용 | `proxy.c`의 `connect_backend()` |
| 12 | `setsockopt` timeout | 적용 | `proxy.c`의 `SO_RCVTIMEO`/`SO_SNDTIMEO` |
| 13 | thread-per-client 서버 | 부분 | 매 연결 스레드 대신 워커 풀 + 이벤트 루프로 대체 |
| 14 | condition variables | 적용 | `proxy.c` 큐의 `pthread_cond_wait`/`pthread_cond_signal` |
| 15 | SoftPrayog select/poll/epoll | 적용 | `docs/notes/iomux.md`와 `src/iomux.c`의 백엔드 분기 |
| 16 | Nir Lichtman minimal web server | 부분 | 정적 파일 서빙 구조. `sendfile` 대신 `read`/`send` 청크 전송 |
| 17 | Thread pools | 적용 | `worker.c` 워커 풀(`worker_pool_start`/`join`) |
| 18 | fork/pipe IPC | 미적용 | 멀티프로세스 안 함(D5) |
| 19~20 | Program your own web server | 적용 | 전체 구조와 모듈 분리(파서/응답/정적 파일) |
| 21 | Proxy vs Reverse Proxy | 적용 | `proxy.c`는 리버스 프록시 역할 |
| 22 | `accept`/`inet_ntop` 클라이언트 주소 | 적용 | `connection.c`의 `getpeername` + `inet_ntop` → 접근 로그 |
| 23 | `fdopen`(소켓=파일) | 미적용 | 쓰지 않음 |
| 24 | Kanich Linux socket API | 적용 | 소켓 흐름 이해의 기반 |
| 25 | 두 종류의 소켓 | 적용 | `SOCK_STREAM`(TCP) 선택 |
| 26 | Progress bars | 미적용 | 범위 밖 |
| 27 | 조앤 Forward/Reverse Proxy vs LB | 적용 | 개념 구분(게이트웨이/로드밸런서) |
| 28 | Kanich processes/threads/io multiplexing | 적용 | 결정 D5, `docs/notes/multiplexing-rationale.md` |
| 29 | BJ.5 프로세스/스레드 용어 | 적용 | 용어 정리 |
| 30 | HTTP/1.1 vs 2 vs 3 | 부분 | HTTP/1.1만 구현. HTTP/2는 앞단 게이트웨이에서 종료(D26) |
| 31 | API Gateway vs LB vs Proxy | 적용 | 결정 D18 |
| 32 | Reverse Proxy vs LB vs GW | 적용 | 개념 |
| 33 | Envoy vs NGINX/HAProxy | 적용 | 결정 D5의 "단일 프로세스 multi-reactor" 근거 |
| 34 | Envoy for microservices | 참고 | 사이드카 개념만 |
| 35 | Envoy crash course | 참고 | 구조 용어 |
| 36 | Envoy vs NGINX | 적용 | 단일 프로세스 멀티스레드 선택의 근거 |
| 37 | Envoy 공식 문서 | 미적용 | 범위 밖 |
| 38 | GFM 가이드 | 적용 | 문서 작성 형식 |

## 코드에 바로 보이는 적용

- **논블로킹 + 멀티플렉싱**(1, 8, 15): `src/iomux.c`, `src/worker.c`. 워커가 `iomux_wait()`로 준비된 fd만 처리.
- **워커 풀**(13, 17): `src/worker.c`의 `pthread_create` N개. 연결당 스레드는 만들지 않음.
- **소켓 기본**(2, 19~20, 24, 25): `src/tcpserver.c`.
- **클라이언트 주소**(22): `connection_init()`의 `getpeername` + `inet_ntop`.
- **백엔드 클라이언트**(10, 11, 12): `src/proxy.c`.
- **큐 + 조건 변수**(14): `src/proxy.c`의 `g_head`/`g_tail` + `pthread_cond_wait`.
- **정적 파일 서빙**(16): `src/static_handler.c`.
- **동시성 모델 선택**(28, 29, 33, 36): `docs/decisions/STEP05_CONCURRENCY_DECISIONS.md` D5.

## 참고만 하고 안 만든 것

- UDP(3, 9) — 범위 밖.
- 체크섬(5) — 커널이 처리.
- libcurl(6) — 외부 라이브러리 금지.
- `fork`/`pipe` IPC(18) — 공유 좌석을 스레드+뮤텍스로 해결(D5).
- `sendfile`(16) — 대신 `read`/`send` 청크 전송.
- HTTP/2(30) — 앞단 게이트웨이에서 종료.
- Envoy 설정(37) — 범위 밖.
