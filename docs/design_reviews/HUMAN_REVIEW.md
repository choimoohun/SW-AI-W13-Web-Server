# 사람이 검토해서 정한 것

여기에는사람이 대화에서 검토하고 확정한 내용을 적습니다.
자동 검증이 잡은 문제는 [../verify/AUTO_FINDINGS.md](../verify/AUTO_FINDINGS.md)에,
실행 기록은 [../verify/VERIFICATION_LOG.md](../verify/VERIFICATION_LOG.md)에 있습니다.

| 검토 내용 | 사람이 정한 것 | 반영 |
|---|---|---|
| "멘토 권장" 같은 표현 | 결정 근거에서 사람을 빼고 문제와 제약만 적기 | `DECISIONS.md` |
| "서버 배관", "배선" | 어색한 비유를 걷어내고 동작 이름으로 바꾸기 | `README.md`, STEP 문서 |
| `tcpserver_listen()`, `nc -zv` | 함수 이름과 명령어는 백틱으로 구분 | 전 문서 |
| "직렬화", "소유권 계약" | 읽는 사람이 모르는 용어는 먼저 확인하고 풀어 쓰기 | `DESIGN.md` |
| 외부 CDN 서술 | 로컬 프로젝트에서 근거 없는 외부 서비스 설명 제거, 로컬 TTL 캐시만 유지 | `DECISIONS.md` |
| 요구사항의 문제 정의 | 기능 이름이 아니라 푸는 문제로 적되, 역사적 출발점 주장은 하지 않기 | `REQUIREMENTS.md` |
| "멱등키" | 사용자 문서는 "중복 방지 키", 코드는 `idempotency_key` 유지 | STEP10 |
| "결제하기를 누르면 바로 결제 페이지" | 버튼을 누르면 별도 결제 화면으로 이동 | STEP10 |
| 100동시 수용 기준 | `make load-test-heavy`로 1000 요청을 명시적으로 확인 | STEP08, 검증 매트릭스 |
| 환불 정책 | 출발 1일 전까지 마감, 100/80/50% 구간 | STEP10 |
| 동시성 구조 | 워커 스레드별 이벤트 루프 + Linux에서 affinity, Envoy식 선택 | `DECISIONS.md` D5 |
| CodeRabbit 의견 | 로그로 남기고 사람 확인 후 반영 | `agent/AGENT_WORKFLOW.md` |
| "협업"의 뜻 | 사람과 사람의 협업만 `TEAM_RULES.md`, AI 작업은 `agent/` | 문서 구조 |

이 문서는 사람이 판단한 자리입니다.
AI가 작성한 구조 검토는 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)와 [DESIGN_REVIEW.md](DESIGN_REVIEW.md)에 있습니다.
