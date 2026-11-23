# 하루 작업 범위

오늘 무엇을 하고, 무엇을 미루는지 적습니다.
영구 규칙은 [TEAM_RULES.md](../TEAM_RULES.md), 설계는 상위 `docs/`, 실행 증거는 [VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)에 둡니다.

## 지금 상태

요구사항 6단계(STEP1~6)와 확장 단계(EXT01~04)까지 마쳤습니다. 브랜치는 `eungi-dev`입니다.
2026-11-23: 항공편 시드의 출발지를 11개 공항으로 넓히고, 문서 리네임 이후 깨진 링크 16개를 정리했습니다.

2026-11-23 후반에 한 일:

- I/O 멀티플렉싱과 이벤트 루프 문답 정리 — [iomux.md](../notes/iomux.md) §0, [AI_QA_LOG.md](AI_QA_LOG.md).
- 검토 과제를 테스트 케이스로 정리 — `tests/integration/review_cases.sh` (`make review-cases`).
  TC1 트래픽 급증(1000/1000), TC2 느린 클라이언트(200, 24ms) PASS. TC3·TC4는 미구현.
- 문서 통합 — `why_webserver.md`→[REQUIREMENTS.md](../REQUIREMENTS.md),
  `AUTO_FINDINGS.md`→[VERIFICATION_MATRIX.md](../verify/VERIFICATION_MATRIX.md),
  `CURRENT_ARCHITECTURE.md`→[DESIGN.md](../DESIGN.md), `HUMAN_REVIEW.md`→[DECISIONS.md](../DECISIONS.md),
  `notes/`와 `study/`→[study_notes/](../notes).
- 부하 테스트 수치는 [VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)를 기준으로 본다.

## 실제로 확인한 것

- `make test`, `make integration`, `make integration-req`, `make integration-backends`, `make integration-payments`, `make edge-cases`
- `make load-test-heavy`: 동시 100, 요청 1000, Non-2xx 0
- `make mutation`, `make asan`, `make leak`
- Linux epoll 회귀 `tests/linux_check.sh`

출력은 [../records/VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)에 있습니다.

## 다음 작업

### 2026-11-24 예산: 2.5단위 / 요청 예산 18회(예비 2)

1단위 = 실행 가능한 확인 명령 하나로 닫히는 변경.

| 단계 | 작업 | 단위 | 요청 예산 | 끝나는 증거 |
|---|---|---|---|---|
| t1 | 예약·결제 일관성 — 좌석 `HOLD` → 결제 성공 시 `CONFIRM`, 실패·만료 시 복원 | 2.0 | 7 | `make review-cases` TC3 PASS |
| t2 | 프록시 큐 상한 + `503`(`src/proxy.c` enqueue 한 지점) | 0.5 | 2 | 큐가 차면 즉시 `503`, 백엔드 과부하 전파 차단 확인 |
| t3 | 문서·PR 정리(`AI_QA_LOG.md`, 검증 결과 반영) | 0.5 | 4 | PR 하나로 되돌릴 수 있는 상태 |
| t4 | 시연 검증·부하 수치 확인 | — | 3 | `docs/records/VERIFICATION_LOG.md` 갱신 |

범위 밖: 로그인/세션(인증·인가), 멀티프로세스 + epoll 실험, Envoy/TLS 실험. 이 셋은 별도 브랜치에서 한다.

### 끝난 것 (2026-11-23)

- I/O 멀티플렉싱 선택 이유 정리 — `docs/notes/iomux.md`, `docs/DECISIONS.md`의 "대안 비교"
- 문서 압축 정리 — `why_webserver`→`REQUIREMENTS`, `AUTO_FINDINGS`→`VERIFICATION_MATRIX`, `CURRENT_ARCHITECTURE`→`DESIGN`, `notes/`+`study/` 통합
- 검토 과제 테스트 케이스화 — `tests/integration/review_cases.sh` (`make review-cases`)

## 계획 (준비만)

- 30초 데모 영상. 앱을 실제로 돌리면서 영어 자막을 얹는 광고 형태.
  - 화면: `www/flights.html`에서 항공편 검색 → 예약 → 좌석 감소, `www/ops.html`에서 출발/도착 상태 변화.
  - 자막 예시: `socket() → bind() → listen() → accept()`, `100 concurrent requests, only one seat sold`.
  - 지금은 계획만 두고 제작하지 않는다.

## 아직 하지 않은 것

인증/인가, TLS/HTTP2, Forward Proxy, CGI, 물리 링크 장애.

## 아키텍처 완료·남은 일

구조 관련 확정 사항과 남은 작업이다. 상세 구조 설명은
[DESIGN.md](../DESIGN.md)의 "현행 구조 메모" 절을 본다.

완료한 일:

1. `iomux` 백엔드 분리 — Linux `epoll` / macOS `kqueue` / `poll` 폴백.
   `tests/linux_check.sh`로 재검증.
2. `proxy_handle()` 블로킹 제거 — 전용 스레드 + 큐. 느린 백엔드 2065ms 동안 `/health` 0ms.
3. 전역 락 경합 완화 — 항공편별 락 + 메타 락, 파일 저장은 스냅샷 후 락 밖에서 수행.
4. 정적 캐시 헤더 — `Cache-Control`, 약한 `ETag`. 외부 연동이 아니라 로컬 캐시 정책 검증용.
5. Linux valgrind 재검증 — `make leak` in use at exit 0 bytes, ERROR SUMMARY 0.

남은 일:

1. `flight_api.c` / `proxy.c`의 책임 단위 분리(JSON·라우터·핸들러, 라우터·백엔드풀·스레드).
2. 정적 파일 I/O의 진짜 비동기화(`io_uring`, POSIX AIO) — 이식성과 트레이드오프 검토 필요.
3. `cdn_purge()` 호출 경로 연결 또는 TTL-only로 확정.
4. 인증 도입 시 client id 위조 문제.
5. 선택 실험: `SO_REUSEPORT` / `EPOLLEXCLUSIVE`로 accept 단계 wake-up 비용 측정.

## 기록 방식

문제 → 설계 → 검증 순서로 적습니다.
실행하지 않은 테스트는 `NOT RUN`, 범위 밖은 `OUT OF SCOPE`으로 둡니다.
팀 합의가 필요한 항목은 여기서 제외하고 [TEAM_RULES.md](../TEAM_RULES.md)의 미정 항목에 둡니다.

## 리밋 운영

작업 범위가 클 때는 `tools/rate_limit_manager.py`로 하루 목표를 나눕니다.
사용법은 [rate_limit_manager 안내](tools/readme-rate-limit-manager.md)에 있습니다.