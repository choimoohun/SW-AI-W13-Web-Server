# Architectural Decisions (ADR)

프로젝트 개발 과정에서 확정한 기술 결정과 트레이드오프를 STEP1~6으로 나누어 기록합니다. 각 단계를 열면 세부 결정이 나옵니다.

| 단계 | 문서 | 결정 수 |
|---|---|---|
| TCP 소켓 서버 | [STEP01_TCP_SERVER_DECISIONS.md](decisions/STEP01_TCP_SERVER_DECISIONS.md) | 1 |
| HTTP 요청 파싱 | [STEP02_HTTP_PARSER_DECISIONS.md](decisions/STEP02_HTTP_PARSER_DECISIONS.md) | 1 |
| HTTP 응답 생성 | [STEP03_HTTP_RESPONSE_DECISIONS.md](decisions/STEP03_HTTP_RESPONSE_DECISIONS.md) | 3 |
| 정적 파일 서빙 | [STEP04_STATIC_FILE_DECISIONS.md](decisions/STEP04_STATIC_FILE_DECISIONS.md) | 2 |
| 동시 요청 처리 | [STEP05_CONCURRENCY_DECISIONS.md](decisions/STEP05_CONCURRENCY_DECISIONS.md) | 7 |
| 동적 요청 처리 | [STEP06_FLIGHT_DEMO_DECISIONS.md](decisions/STEP06_FLIGHT_DEMO_DECISIONS.md) | 14 |

중복 기록은 대표 단계에 두고, 같은 내용이면 원본 ID를 함께 적었습니다(D9/D21, D6/D23).

## 왜 이 기술을 골랐는가

표의 "배경 및 사유" 열이 각 결정의 이유입니다. 이 절은 단계를 관통하는 선택만 모읍니다.

- 언어·라이브러리 — C11과 POSIX 시스템 API만 사용. 외부 HTTP/이벤트 루프 라이브러리 금지가 과제 제약(C-02)이라 파서·이벤트 루프·JSON을 직접 만들었습니다. 목표가 "동작을 설명할 수 있는 코드"라서 라이브러리로 감추지 않았습니다.
- 동시성 — 멀티프로세스가 아니라 워커 스레드 N개 + 멀티플렉싱(D5). 이유는 공유 상태(좌석) 구현 비용입니다. 프로세스로 가면 좌석 사본이 워커 수만큼 생겨 공유 메모리나 IPC가 필요합니다. 대가는 크래시 격리 부재입니다. 비교는 [notes/iomux.md](notes/iomux.md)를 봅니다.
- 멀티플렉싱 백엔드 — `epoll`(Linux)/`kqueue`(macOS)을 기본, `poll`을 폴백(D9, D21). 준비된 fd만 받는 확장성과 POSIX 표준의 이식성을 같이 잡기 위해서입니다. 레벨 트리거를 쓰고 `EPOLLET`는 쓰지 않습니다.
- 프로토콜 — HTTP/1.1만 직접 구현. `chunked`는 501(D3), HTTP/2와 TLS는 앞단 게이트웨이에서 종료(D26). 서버는 평문 HTTP/1.1 엔진으로 남깁니다.
- 식별·인증 — 인증은 없습니다. 브라우저 `client id`를 그대로 받습니다(D13). 위조 가능성을 대가로 두고 "내 예약" 목록과 취소 소유자 확인만 구현했습니다. 실제 인증은 별도 과제입니다.
- 서비스 경계 — 예약·좌석·운항 추적은 C 서버 안에 두고(D27), 캐시·DB·결제는 별도 프로세스로 연결(D28). 이 경계를 넘으면 `flight_t.mu`가 통하지 않아 DB 트랜잭션으로 다시 설계해야 합니다.
- 큐 — HTTP 메시지 큐는 두지 않습니다. 워커↔프록시 스레드 사이의 작업 큐만 있습니다. 메시지 큐가 푸는 문제(서비스 간 커플링·백프레셔)와 HTTP/1.1 HOL blocking은 다른 문제입니다.

### 대안 비교

각 분야에서 무엇을 후보로 두고 왜 이것을 골랐는지 정리합니다. 근거 상세는 [notes/iomux.md](notes/iomux.md)와 각 STEP 문서에 있습니다.

| 분야 | 후보 | 선택 | 왜 이것인가 |
|---|---|---|---|
| I/O 감시 | 논블로킹 폴링 / `select` / `poll` / `epoll`·`kqueue` | `epoll`(Linux)·`kqueue`(macOS) + `poll` 폴백 | 준비된 fd만 받아 전체 스캔을 피하고, POSIX 표준 폴백으로 이식성도 확보 |
| 실행 주체 | 멀티프로세스 / 스레드 풀 / 단일 스레드 + 루프 | 워커 스레드 4개, 각자 멀티플렉싱 | 좌석 공유 상태를 `fork`로 찢으면 사본과 IPC 비용이 생김 |
| 프로토콜 | HTTP/1.1 / HTTP/2 / TLS 직접 구현 | HTTP/1.1만 직접, h2·TLS는 앞단 게이트웨이 | C 파서를 다시 만들지 않고 끝단에 맡김(D3, D26) |
| 언어·라이브러리 | C11+POSIX / Go·Rust / Node 계열 | C11 + POSIX만 | 외부 HTTP·이벤트 루프 라이브러리 금지(C-02), 동작을 직접 설명 |
| `/api` 큐 | 메시지 큐 / 인메모리 작업 큐 / 큐 없음 | 워커↔프록시 인메모리 큐 | 같은 프로그램 안의 작업 전달에는 메시지 큐보다 가볍고 충분 |

## 보안 정책
- 경로 검증: 모든 파일 요청은 `realpath`를 통해 해석된 뒤, 서버 루트 하위에 있는 경우에만 처리됩니다. (Path Traversal 방지)
- 에러 핸들링: 403, 404 등 민감한 정보 유출을 막기 위해 표준 상태 코드만을 반환하며, 상세 시스템 정보는 응답에서 제외합니다.

## 발표 후 과제 — 예약·결제 일관성 (D29, 미구현)

2026-11-24에 설계만 확정하고 구현은 미뤘습니다. 코드는 아직 `book`이 즉시 좌석을 차감합니다.

- **좌석 차감 시점** — `HOLD`에서 차감한다. 초과 예약 방지가 복원보다 우선이다. 실패·만료는 `flight_store`의 `store_tick` 만료 스윕이 좌석을 되돌린다.
- **confirm 트리거** — 브라우저가 결제 성공 후 `confirm` API를 호출한다. 결제 백엔드(8500)를 수정하지 않는다.
- **만료 시간** — 설정 항목, 기본 60초.
- **알려진 위험** — 결제는 성공했는데 브라우저가 닫히면 "결제됨 + HOLD 만료"가 된다. 환불·대사(reconciliation) 절차가 별도로 필요하며 다음 단계 과제다.

재검토 시 확인할 것: `flight_t`가 `held`/`confirmed` 두 상태를 갖는지, 스윕이 어느 주기로 도는지,
`confirm` 실패와 만료의 순서(확정 직후 스윕 경쟁)다.
