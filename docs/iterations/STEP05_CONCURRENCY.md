# STEP5 — 동시성: Envoy식 multi-reactor (D5)

## 한 줄
워커 스레드가 각자 이벤트 루프를 돌려 동시 요청을 처리한다.

## 범위
- 워커 스레드 N개, 각자 iomux(poll/epoll/kqueue) 이벤트 루프. 단일 프로세스.
- accept 분산 기본안: 공유 listen fd + nonblocking accept + EAGAIN 무시.
- 어피니티: Linux 선택(#ifdef), macOS no-op.
- 정적 서빙 동작은 개조 전후 동일해야 한다(`make integration` 기준).

## 스캐폴딩 파일
- `include/iomux.h` / `src/iomux.c` — 멀티플렉싱 추상화. `iomux_create()`, `iomux_add()`, `iomux_mod()`, `iomux_del()`, `iomux_wait()`.
  poll을 기본으로 쓰고 `#ifdef`로 epoll(Linux)/kqueue(macOS)를 선택한다.
- `include/worker.h` / `src/worker.c` — 워커 풀/루프 골격. `worker_pool_start()`, `worker_pool_join()`, `accept_ready()`, `on_readable()`, `on_writable()`. 한국어 주석 + TODO.
- `src/main.c` / `src/connection.c`는 건드리지 않음(직접 구현 영역). `connection_feed()`, `connection_flush()`만 연결 상태를 담당.

## 작업 순서 (Todo)
1. [x] `iomux` poll 백엔드 완성: `iomux_create()`, `iomux_add()`, `iomux_mod()`, `iomux_del()`, `iomux_wait()`. macOS에서 동작 확인.
2. [x] `connection` 상태 머신 개조: 연결 컨텍스트 + `connection_feed()` / `connection_flush()` 분리, keep-alive 지원.
       `http_parser_parse()`의 `PARSE_COMPLETE` / `PARSE_NEED_MORE` / `PARSE_ERROR`를 활용하고 `req.consumed`로 요청 경계 확정.
3. [x] `worker` 루프 완성: `accept_ready()`, `on_readable()`, `on_writable()`, 유휴 타임아웃, `close_conn()` 단일 정리.
4. [x] `worker_pool_start()` / `worker_pool_join()` 완성(`pthread_create()`, `pthread_join()`, 실패 회수).
5. [x] `main.c` 배선: accept 루프 → `worker_pool_start(listen_fd, N, doc_root, &g_stop)`
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
- 100동시 예약: after `201 1 / 409 99`, before `201 4 / 409 96`(3회 연속 재현).
- `make tsan`: after 경고 0건, before `WARNING: ThreadSanitizer: data race` 1건(`flight_store.c:420`).
- 종료: SIGTERM → 워커 join → listen fd close, 잔여 프로세스 없음.

## 관련 결정
- D5, D9, D10, D20, D21, D22, D25 — [STEP05 결정](../decisions/STEP05_CONCURRENCY_DECISIONS.md)

## 기록 원칙
- 구현하지 않은 기능을 완료로 기록하지 않는다. 검증은 실행 결과로만 남긴다.
