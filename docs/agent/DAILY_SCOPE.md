# 하루 작업 범위

오늘 무엇을 하고, 무엇을 미루는지 적습니다.
영구 규칙은 [TEAM_RULES.md](../TEAM_RULES.md), 설계는 상위 `docs/`, 실행 증거는 [VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)에 둡니다.

## 지금 상태

요구사항 6단계(STEP1~6)와 확장 단계(EXT01~04)까지 마쳤습니다. 브랜치는 `eungi-dev`입니다.
2026-11-23: 항공편 시드의 출발지를 11개 공항으로 넓히고, 문서 리네임 이후 깨진 링크 16개를 정리했습니다.

## 실제로 확인한 것

- `make test`, `make integration`, `make integration-req`, `make integration-backends`, `make integration-payments`, `make edge-cases`
- `make load-test-heavy`: 동시 100, 요청 1000, Non-2xx 0
- `make mutation`, `make asan`, `make leak`
- Linux epoll 회귀 `tests/linux_check.sh`

출력은 [../records/VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)에 있습니다.

## 다음 작업

오늘(2026-11-23) 목표는 I/O 멀티플렉싱과 이벤트 루프를 확실히 잡는 것이다. 인증/인가와 Forward Proxy는 오늘 범위 밖이다.

1. [완료 2026-11-23] I/O 멀티플렉싱 선택 이유 정리 — `docs/notes/iomux.md` 신설, `docs/decisions/DECISIONS.md`에 "왜 이 기술을 골랐는가" 추가
2. 미결제 예약 TTL과 좌석 복원
3. 프록시 큐 상한과 `429`/`503`
4. 로그인/세션 (인증/인가 — 별도 과제로 미뤄짐)

## 계획 (준비만)

- 30초 데모 영상. 앱을 실제로 돌리면서 영어 자막을 얹는 광고 형태.
  - 화면: `www/flights.html`에서 항공편 검색 → 예약 → 좌석 감소, `www/ops.html`에서 출발/도착 상태 변화.
  - 자막 예시: `socket() → bind() → listen() → accept()`, `100 concurrent requests, only one seat sold`.
  - 지금은 계획만 두고 제작하지 않는다.

## 아직 하지 않은 것

인증/인가, TLS/HTTP2, Forward Proxy, CGI, 물리 링크 장애.

## 기록 방식

문제 → 설계 → 검증 순서로 적습니다.
실행하지 않은 테스트는 `NOT RUN`, 범위 밖은 `OUT OF SCOPE`으로 둡니다.
팀 합의가 필요한 항목은 여기서 제외하고 [TEAM_RULES.md](../TEAM_RULES.md)의 미정 항목에 둡니다.

## 리밋 운영

작업 범위가 클 때는 `tools/rate_limit_manager.py`로 하루 목표를 나눕니다.
사용법은 [rate_limit_manager 안내](tools/readme-rate-limit-manager.md)에 있습니다.