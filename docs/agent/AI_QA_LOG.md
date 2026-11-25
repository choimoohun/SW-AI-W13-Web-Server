# AI 질의 기록 — 문답에서 확인된 결정 목록

AI에게 질의하고 실행으로 검증한 뒤 채택·기각한 결정만 모은다.
풀이와 개념 설명은 [../notes/runtime-internals.md](../notes/runtime-internals.md)과 [../notes/iomux.md](../notes/iomux.md)에,
사람이 정한 것은 [../DECISIONS.md](../DECISIONS.md), 실행 중 발견한 결함은 [../records/ERROR_FIX_HISTORY.md](../records/ERROR_FIX_HISTORY.md)에 둔다.

## 의사결정

| 분야 | 질의 | 근거·검증 | 결론 | 반영 |
|---|---|---|---|---|
| 실행 주체 | `fork` 멀티프로세스 + 멀티플렉싱이 맞는가 | 좌석을 프로세스마다 두면 공유 메모리·IPC 필요, 크래시 격리만 이득 | 단일 프로세스 + 워커 스레드 N개 | D5 |
| I/O 감시 | `poll`이어도 되는가 / `select`가 나은가 | `epoll`/`kqueue`는 준비된 fd만 받음, `select`는 `FD_SETSIZE`·전체 스캔 | epoll(Linux)·kqueue(macOS) + poll 폴백 | D9, D21 |
| 이식성 | 하이브리드(멀티프로세스+epoll)가 정답인가 | 코어 사용·격리는 좋지만 공유 상태가 없어야 함 | 공유 좌석이 있으므로 스레드로 대체, 크래시 격리 포기 | D5 |
| 연결 | 이벤트당 `accept`를 여러 번 해도 되나 | `EAGAIN`까지 반복하면 한 워커가 독점 → 동시성 데모 소멸 | 이벤트당 1건 | D10 |
| 연결 | `send_all`처럼 부분 쓰기를 처리하려면 | short write가 가능하므로 남은 바이트 루프 필요 | 부분 쓰기 처리(실무 용어는 partial write) | `src/http_response.c` |
| 큐 | HTTP 메시지 큐가 필요한가 | 요구사항에 없고, 워커↔프록시 작업 큐로 충분 | 기각 | — |
| 큐 | HTTP/1.1 HOL은 메시지 큐로 푸는가 | 다른 문제(커플링·백프레셔 대 전송 계층 HOL) | 구분해서 접근 | `../notes/runtime-internals.md` §10 |
| 프록시 | 워커가 백엔드를 직접 호출하면 | 느린 백엔드가 워커를 점유 → `/health`까지 막힘 | 전용 스레드 + 큐 + `dup()` | D22 |
| 동시성 용어 | 멀티스레드와 멀티플렉싱은 비교 단위인가 | 처리 주체 수 vs I/O 감시 방식 | 둘은 조합 가능한 축 | `../notes/runtime-internals.md` §4 |
| 동시성 용어 | “epoll이 감시 중”의 실체 | 스레드가 도는 것이 아니라 소켓 대기 큐에 콜백을 건 것 | 준비 알림(readiness)만 받음 | `../notes/iomux.md` |
| 동시성 용어 | 논블로킹과 멀티플렉싱은 다른가 | 논블로킹은 `recv()`가 `EAGAIN` 반환, 멀티플렉싱은 커널이 ready 목록 통지 | 서로 보완하는 계층 | `../notes/runtime-internals.md` |
| 지연 | 각 요청 대기 시간은 어떻게 줄어드는가 | 워커 타임라인이 “준비된 이벤트”로 채워져 I/O 대기만 겹침 | CPU 연산은 겹지 않음 | `make load-test-heavy` |
| 지연 | HTTP/1.1 슬로우 스타트를 극복하려면 | TCP 혼잡 제어라 끌 수 없음 → 영향 줄이는 방향 | keep-alive(적용), OS 튜닝·h2는 범위 밖 | `include/http_response.h` |
| 프로토콜 | 멀티플렉싱으로 HTTP/2가 되는가 | 안 됨. h2는 HPACK·프레임·스트림 상태 머신 | 앞단 게이트웨이에서 종료 | D26 |
| 프로토콜 | CGI/FastCGI는 범위인가 | FR-05는 안 B(내장 핸들러+프록시) 채택, 외부 프로세스 도구 | 안 A·FastCGI 범위 밖 | `../REQUIREMENTS.md` |
| 범위 | Next.js·SSG·SOA·REST | 프레임워크는 범위 밖, REST 스타일은 관통 | 프레임워크 미도입 | — |
| 파일 I/O | 정적 파일 I/O를 비동기화해야 하나 | 병목은 디스크가 아니라 공유 좌석 락 | 선택 실험으로만 | `../DESIGN.md` |
| 인증 | `client id` 위조는 | 인증 없음이 대가, “내 예약” 목록·취소 소유자 확인만 | 실제 인증은 별도 과제 | `src/flight_api.c` |
| 구조 표현 | “Envoy식 multi-reactor” 표현은 | shared-nothing이 아니라 좌석 공유라 오해 | “워커별 이벤트 루프”로 변경 | `../DESIGN.md` |

## 실행으로 확인한 숫자

- 느린 백엔드 2065ms 동안 `/health` 0ms → 프록시 비차단.
- 100동시 예약 after `201 1 / 409 99`, before `201 4 / 409 96` → 초과 예약 재현·차단.
- `make test`, `make integration`, `make integration-req` 회귀 없음.
- `make leak` in use at exit 0 bytes, ERROR SUMMARY 0.

## 테스트 케이스화

- TC1 트래픽 급증, TC2 느린 클라이언트, TC3 예약·결제 일관성, TC4 워커 장애 격리 → [../../tests/integration/review_cases.sh](../../tests/integration/review_cases.sh) (`make review-cases`).
