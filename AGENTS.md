# AGENTS.md

AI 코딩 에이전트(Hermes, Claude Code 등)가 이 저장소에서 먼저 읽는 파일입니다.
에이전트 작업 규칙은 이 파일 하나에만 둡니다. 작업 기록(AI 문답, 하루 범위, 도구)은 `docs/agent/`에 있습니다.

## 프로젝트

C11 + POSIX만으로 만든 HTTP/1.1 웹서버. 워커 스레드마다 event loop(epoll/kqueue/poll),
업스트림 호출은 전용 프록시 스레드. 구조는 [docs/DESIGN.md](docs/DESIGN.md).

## 읽는 순서

1. [README.md](README.md)
2. [docs/agent/DAILY_SCOPE.md](docs/agent/DAILY_SCOPE.md) — 오늘 범위
3. [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md)
4. [docs/DESIGN.md](docs/DESIGN.md), [docs/DECISIONS.md](docs/DECISIONS.md)
5. [docs/verify/VERIFICATION_MATRIX.md](docs/verify/VERIFICATION_MATRIX.md), [docs/records/VERIFICATION_LOG.md](docs/records/VERIFICATION_LOG.md)

팀 규칙은 [docs/TEAM_RULES.md](docs/TEAM_RULES.md)에 있습니다.

## 어디에 무엇이 있나

| 하는 일 | 이 저장소에서 |
|---|---|
| 요구사항(spec) | `docs/REQUIREMENTS.md` (FR·AC) |
| 작업 계획(plan) | `docs/iterations/STEPxx_*.md`, `docs/agent/DAILY_SCOPE.md` |
| 결정 기록(ADR) | `docs/DECISIONS.md`, `docs/decisions/` |
| 검증 기록 | `docs/verify/VERIFICATION_MATRIX.md`, `docs/records/VERIFICATION_LOG.md` |
| AI 문답 기록 | `docs/agent/AI_QA_LOG.md` |
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
- 미정 정책 확정
- 실행하지 않은 테스트를 PASS로 적기
- 실패와 수정 기록 지우기
- `docs/TEAM_RULES.md`, `docs/DECISIONS.md`에서 확정된 것을 리뷰 하나로 뒤집기

## CodeRabbit

PR 코멘트에서 사용하는 명령입니다. 제안은 로컬 명령으로 확인한 뒤 반영합니다.

```text
@coderabbitai review
@coderabbitai full review
@coderabbitai pause / resume
@coderabbitai resolve
```

Autofix처럼 봇이 브랜치에 직접 커밋하는 기능은 쓰지 않습니다. 위 "사람 체크" 단계를 건너뛰기 때문입니다.
