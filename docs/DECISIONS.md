# Architectural Decisions (ADR)

프로젝트 개발 과정에서 확정한 기술 결정과 트레이드오프를 STEP1~6으로 나누어 기록합니다. 각 단계를 열면 세부 결정이 나옵니다.

| 단계 | 문서 | 결정 수 |
|---|---|---|
| STEP1 — TCP 소켓 서버 | [STEP01_TCP_SERVER_DECISIONS.md](decisions/STEP01_TCP_SERVER_DECISIONS.md) | 1 |
| STEP2 — HTTP 요청 파싱 | [STEP02_HTTP_PARSER_DECISIONS.md](decisions/STEP02_HTTP_PARSER_DECISIONS.md) | 1 |
| STEP3 — HTTP 응답과 연결 수명 | [STEP03_HTTP_RESPONSE_DECISIONS.md](decisions/STEP03_HTTP_RESPONSE_DECISIONS.md) | 3 |
| STEP4 — 정적 파일 서빙 | [STEP04_STATIC_FILE_DECISIONS.md](decisions/STEP04_STATIC_FILE_DECISIONS.md) | 2 |
| STEP5 — 동시 요청 처리 | [STEP05_CONCURRENCY_DECISIONS.md](decisions/STEP05_CONCURRENCY_DECISIONS.md) | 7 |
| STEP6 — 항공편 예약 데모 | [STEP06_FLIGHT_DEMO_DECISIONS.md](decisions/STEP06_FLIGHT_DEMO_DECISIONS.md) | 14 |

중복 기록은 대표 단계에 두고, 같은 내용이면 원본 ID를 함께 적었습니다(D9/D21, D6/D23).

## 보안 정책
- 경로 검증: 모든 파일 요청은 `realpath`를 통해 해석된 뒤, 서버 루트 하위에 있는 경우에만 처리됩니다. (Path Traversal 방지)
- 에러 핸들링: 403, 404 등 민감한 정보 유출을 막기 위해 표준 상태 코드만을 반환하며, 상세 시스템 정보는 응답에서 제외합니다.
