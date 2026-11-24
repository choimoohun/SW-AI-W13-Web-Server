# AGENTS.md

AI 코딩 에이전트(Claude Code, Hermes 등)가 이 저장소에서 먼저 읽는 파일입니다.
자세한 읽는 순서와 금지 사항은 [docs/agent/AGENT_WORKFLOW.md](docs/agent/AGENT_WORKFLOW.md)를 따릅니다.

## 프로젝트

C11 + POSIX만으로 만든 HTTP/1.1 웹서버. 워커 스레드마다 event loop(epoll/kqueue/poll),
업스트림 호출은 전용 프록시 스레드. 구조는 [docs/DESIGN.md](docs/DESIGN.md).

## 어디에 무엇이 있나

| 하는 일 | 이 저장소에서 |
|---|---|
| 요구사항(spec) | `docs/REQUIREMENTS.md` (FR·AC) |
| 작업 계획(plan) | `docs/iterations/STEPxx_*.md`, `docs/agent/DAILY_SCOPE.md` |
| 결정 기록(ADR) | `docs/DECISIONS.md`, `docs/decisions/` |
| 검증 기록 | `docs/verify/VERIFICATION_MATRIX.md`, `docs/records/VERIFICATION_LOG.md` |
| PR 리뷰 체크리스트 | `docs/records/pr_reviews/PR-<번호>.md` |

## 명령

```sh
make                      # 빌드 (-Werror)
make test                 # 단위 테스트
make integration          # 통합 테스트
make integration-req      # 헤더·health·프록시·CDN
make edge-cases           # 경계 입력
make asan / make tsan     # 메모리 오류 / 데이터 경쟁
make pr-review PR=<번호>  # PR 리뷰를 체크리스트로 모으기
```

## 작업 루프

- 기능 추가·버그 수정: 실패하는 테스트 먼저 → 구현 → `make test` + 관련 `make integration-*` → 검증 기록.
- PR 리뷰 반영: [.claude/skills/pr-review-loop/SKILL.md](.claude/skills/pr-review-loop/SKILL.md)
  (fetch → triage → 사람 체크 → apply).

## 하지 말 것

- 요청 없는 커밋·푸시·PR 생성·PR 코멘트
- 실행하지 않은 테스트를 PASS로 적기
- 실패와 수정 기록 지우기
- `docs/TEAM_RULES.md`, `docs/DECISIONS.md`에서 확정된 것을 리뷰 하나로 뒤집기
