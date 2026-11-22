# STEP5 — 동시성: Envoy식 multi-reactor (D5)

## 범위
- 워커 스레드 N개, 각자 iomux(poll/epoll/kqueue) 이벤트 루프. 단일 프로세스.
- accept 분산 기본안: 공유 listen fd + nonblocking accept + EAGAIN 무시.
- 어피니티: Linux 선택(#ifdef), macOS no-op.
- 정적 서빙 동작은 개조 전후 동일해야 한다(make integration 기준).

## 스캐폴딩 파일
- include/iomux.h / src/iomux.c — 멀티플렉싱 추상화. poll 먼저, epoll/kqueue는 #ifdef 추가.
- include/worker.h / src/worker.c — 워커 풀/루프 골격. 한국어 주석 + TODO.
- main.c / connection.c는 건드리지 않음(직접 구현 영역).

## 작업 순서 (Todo)
1. [x] iomux poll backend 완성(create/add/mod/del/wait). macOS에서 동작 확인.
2. [x] connection 상태 머신 개조: 컨텍스트 + feed/flush 분리, keep-alive 지원.
       http_parser의 COMPLETE/NEED_MORE/ERROR를 그대로 활용하고 consumed로 요청 경계 확정.
3. [x] worker 루프 완성: accept_ready/on_readable/on_writable/유휴 타임아웃/단일 정리.
4. [x] worker_pool_start/join 완성(pthread_create/join, 실패 회수).
5. [x] main.c 배선: accept 루프 → worker_pool_start(listen_fd, N, doc_root, &g_stop)
       → g_stop 대기 → worker_pool_join → close(listen_fd) → exit 0.
6. [x] LDLIBS에 -pthread 추가. epoll/kqueue 백엔드는 미구현(poll은 POSIX로 양 OS 동작).
7. [ ] (선택) 어피니티 #ifdef __linux__ / EPOLLEXCLUSIVE / SO_REUSEPORT 실험.

## 검증 계획
- make / make test / make integration: 개조 전후 동일 통과.
- 동시 접속 100+ 유실 없음(FR-06 AC): hey 또는 ab -c 100.
- slow client: nc로 헤더를 천천히 보내는 동안 다른 curl이 즉시 응답(FR-06 AC).
- make asan + Docker valgrind: 누수 0(NFR-01).
- 접근 로그에 워커 ID 포함(NFR-04) → 병렬 처리 증명.

## 데모 시나리오 (항공편 앱, 별도 단계)
- 보드 화면: www/ 정적 + /api/flights 폴링 → 동시 접속/keep-alive 시각화.
- 예약 경쟁: 마지막 1석에 동시 POST 100.
  before(좌석 뮤텍스 없음) = 초과 예약 / after(뮤텍스) = 성공 1건 + 나머지 409.
- 앱 데이터는 시드 JSON + 타이머 시뮬레이션. 외부 API 의존 없음.

## 검증 결과 기록 (macOS arm64, 워커 4개)

- `make` / `make test` / `make integration`: 회귀 없음, 기존 10개 항목 PASS.
- keep-alive: 같은 연결로 두 요청 → `200 200`, 응답 헤더 `Connection: keep-alive`.
- slow client: 헤더 일부만 보낸 연결이 있어도 다른 curl이 2초 내 200 응답.
- 100동시 예약: after `201 1 / 409 99`, before `201 4 / 409 96`(3회 연속 재현).
- `make tsan`: after 경고 0건, before `data race flight_store.c:178` 4건.
- 종료: SIGTERM → 워커 join → listen fd close, 잔여 프로세스 없음.

## 기록 원칙
- 구현하지 않은 기능을 완료로 기록하지 않는다. 검증은 실행 결과로만 남긴다.
