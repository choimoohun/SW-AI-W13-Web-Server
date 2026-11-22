# 하루 작업 범위

오늘 무엇을 하고, 무엇을 미루는지 적습니다.
영구 규칙은 [TEAM_RULES.md](TEAM_RULES.md), 설계는 상위 `docs/`, 실행 증거는 [../verify/README.md](../verify/README.md)에 둡니다.

## 지금 상태

STEP10 결제 서비스와 문서 정리를 끝냈습니다. 브랜치는 `eungi-dev`입니다.

## 실제로 확인한 것

- `make test`, `make integration`, `make integration-req`, `make integration-backends`, `make integration-payments`, `make edge-cases`
- `make load-test-heavy`: 동시 100, 요청 1000, Non-2xx 0
- `make mutation`, `make asan`, `make leak`
- Linux epoll 회귀 `tests/linux_check.sh`

출력은 [../verify/VERIFICATION_LOG.md](../verify/VERIFICATION_LOG.md)에 있습니다.

## 다음 작업

1. 미결제 예약 TTL과 좌석 복원
2. 프록시 큐 상한과 `429`/`503`
3. 로그인/세션

## 아직 하지 않은 것

인증/인가, TLS/HTTP2, Forward Proxy, CGI, 물리 링크 장애.

## 기록 방식

문제 → 설계 → 검증 순서로 적습니다.
실행하지 않은 테스트는 `NOT RUN`, 범위 밖은 `OUT OF SCOPE`으로 둡니다.
팀 합의가 필요한 항목은 여기서 제외하고 [TEAM_RULES.md](TEAM_RULES.md)의 미정 항목에 둡니다.

## 리밋 운영

작업 범위가 클 때는 `tools/rate_limit_manager.py`로 하루 목표를 나눕니다.
사용법은 [tools/README.md](tools/README.md)에 있습니다.
