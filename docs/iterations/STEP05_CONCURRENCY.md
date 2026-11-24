# 동시 요청 처리

## STEP5 - 워커 풀과 iomux 이벤트 루프

## 범위
- 워커 스레드 N개, 각자 iomux(poll/epoll/kqueue) 이벤트 루프. 단일 프로세스.
- accept 분산 기본안: 공유 listen fd + nonblocking accept + EAGAIN 무시.
- 어피니티: Linux 선택(#ifdef), macOS no-op.
- 정적 서빙 동작은 개조 전후 동일해야 한다(`make integration` 기준).

## 파일 구성

| 파일 | 책임 |
|---|---|
| `include/iomux.h` / `src/iomux.c` | 멀티플렉싱 추상화. `iomux_create()`, `iomux_add()`, `iomux_mod()`, `iomux_del()`, `iomux_wait()`. Linux는 epoll, macOS/BSD는 kqueue, 그 외 플랫폼은 poll 폴백 |
| `include/worker.h` / `src/worker.c` | 워커 풀/루프. `worker_pool_start()`, `worker_pool_join()`, `accept_ready()`, `on_readable()`, `on_writable()` |
| `src/main.c` / `src/connection.c` | 수정함. `main.c`는 설정 로드와 `worker_pool_start()`/`worker_pool_join()` 호출, `connection.c`는 `connection_feed()`/`connection_flush()` 상태 머신 |

## 동작 원리

- 워커가 `iomux_wait()`로 준비된 fd만 받습니다. `poll`은 매번 전체를 스캔하고, `epoll`은 준비된 것만 돌려줍니다.
- `READ` 준비 → `read()` → 파서. 완성이면 응답을 만들어 `WRITE` 관심으로 전환.
- `WRITE` 준비 → 남은 응답 `send()`.
- 유휴 5초가 지난 연결은 정리. `listen fd`는 워커가 공유하고 `accept()`는 한 번만.

```mermaid
graph LR
  L[listen fd] --> W[worker iomux]
  W --> R[READ: read + parse]
  R --> P[응답 send_buf]
  P --> W2[WRITE: send]
  W2 --> C[client]
```

## 작업 순서 (Todo)
1. [x] `iomux` poll 백엔드 완성: `iomux_create()`, `iomux_add()`, `iomux_mod()`, `iomux_del()`, `iomux_wait()`. macOS에서 동작 확인.
2. [x] `connection` 상태 머신 개조: 연결 컨텍스트 + `connection_feed()` / `connection_flush()` 분리, keep-alive 지원.
       `http_parser_parse()`의 `PARSE_COMPLETE` / `PARSE_NEED_MORE` / `PARSE_ERROR`를 활용하고 `req.consumed`로 요청 경계 확정.
3. [x] `worker` 루프 완성: `accept_ready()`, `on_readable()`, `on_writable()`, 유휴 타임아웃, `close_conn()` 단일 정리.
4. [x] `worker_pool_start()` / `worker_pool_join()` 완성(`pthread_create()`, `pthread_join()`, 실패 회수).
5. [x] `main.c` 배선: accept 루프를 워커로 이동 → `worker_pool_start(listen_fd, &cfg, &g_stop)`
       → `g_stop` 대기 → `worker_pool_join()` → `close(listen_fd)` → exit 0.
6. [x] `LDLIBS`에 `-pthread` 추가. epoll/kqueue 백엔드도 동일 인터페이스로 구현 완료(poll은 폴백).
7. [ ] (선택) 어피니티 `#ifdef __linux__`, `EPOLLEXCLUSIVE`, `SO_REUSEPORT` 실험.

## 검증 계획
- `make` / `make test` / `make integration`: 개조 전후 동일 통과.
- 동시 접속 100+ 유실 없음(FR-06 AC): `hey` 또는 `wrk -c 100`.
- slow client: `nc`로 헤더를 천천히 보내는 동안 다른 `curl`이 즉시 응답(FR-06 AC).
- `make asan` + Docker valgrind(`make leak`): 누수 0(NFR-01).
- 접근 로그에 워커 ID(`[wN]`) 포함(NFR-04) → 병렬 처리 증명.

## 검증 결과 기록 (macOS arm64, 워커 4개)

- `make` / `make test` / `make integration`: 회귀 없음, 기존 10개 항목 PASS.
- keep-alive: 같은 연결로 두 요청 → `200 200`, 응답 헤더 `Connection: keep-alive`.
- slow client: 헤더 일부만 보낸 연결이 있어도 다른 curl이 2초 내 200 응답.
- 종료: SIGTERM → 워커 join → listen fd close, 잔여 프로세스 없음.

## 관련 결정
- D5, D9, D10, D25 — [STEP05 결정](../decisions/STEP05_CONCURRENCY_DECISIONS.md)

## 알려진 한계
- epoll 백엔드는 커널이 준비된 fd만 돌려주지만(O(ready)), 그 fd의 연결 포인터는 `iomux.c`의 `ptr_for_fd()`가 등록 배열을 선형으로 찾는다. 이벤트 K개, 연결 N개면 한 번의 `iomux_wait`에 O(N·K)다. 워커당 연결이 적은 지금 규모에서는 측정상 문제가 없어 두었고, fd 번호로 바로 찾는 표(fd → 포인터)로 바꾸는 것을 후속 개선으로 남긴다(PR #22 리뷰 R10).

## 기록 원칙
- 구현하지 않은 기능을 완료로 기록하지 않는다. 검증은 실행 결과로만 남긴다.
