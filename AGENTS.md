# AGENTS.md

AI 코딩 에이전트(Hermes, Claude Code 등)가 이 저장소에서 먼저 읽는 파일입니다.
에이전트 작업 규칙은 이 파일 하나에만 둡니다. 스킬은 `docs/agent/skills/`, 작업 기록(AI 문답, 하루 범위, 도구)은 `docs/agent/`에 있습니다.

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

## 작업 루프 — 설계 → 구현 → 검증

| 단계 | 하는 일 | 남기는 곳 | 쓰는 스킬 |
|---|---|---|---|
| 설계 | 요구사항(FR)을 고르고, 바뀌는 설계와 결정을 적는다 | `docs/REQUIREMENTS.md`, `docs/iterations/`, `docs/DESIGN.md`, `docs/decisions/` | [documentation-and-adrs](docs/agent/skills/documentation-and-adrs/SKILL.md) |
| 구현 | 실패하는 테스트를 먼저 만들고 실패를 확인한 뒤 고친다 | `tests/`, `src/` | [test-driven-development](docs/agent/skills/test-driven-development/SKILL.md), 막히면 [debugging-and-error-recovery](docs/agent/skills/debugging-and-error-recovery/SKILL.md) |
| 검증 | 아래 "리버스 엔지니어링 검증"으로 주장마다 코드와 실행으로 확인한다 | `docs/records/VERIFICATION_LOG.md`, `docs/verify/VERIFICATION_MATRIX.md` | [code-review-and-quality](docs/agent/skills/code-review-and-quality/SKILL.md) |
| 리뷰 반영 | PR 리뷰를 같은 방식으로 확인하고, 사람이 체크한 것만 반영한다 | `docs/records/pr_reviews/PR-<번호>.md` | [pr-review-loop](docs/agent/skills/pr-review-loop/SKILL.md) |

검증에서 문제가 나오면 구현으로, 설계가 틀렸으면 설계로 돌아간다. 단계 사이에서 사람이 확인한다.

### 리버스 엔지니어링 검증 (모든 검증 단계의 공통 방식)

1. 확인할 주장을 한 문장으로 쓴다. 예: "파이프라인 요청 두 개의 응답이 순서대로 온다."
2. 그 주장이 걸린 코드를 거꾸로 따라간다. 호출하는 쪽, 헤더 계약, 어느 스레드에서 도는지까지.
3. 가능하면 실행으로 재현한다(`make test`, `make integration`, `make asan`, 짧은 재현 스크립트). 재현하지 못했으면 "코드 근거만"이라고 쓴다.
4. 판정(타당 / 부분 타당 / 해당 없음 / 판단 보류)과 근거(파일:줄)를 남긴다. 방향 결정이 필요한 것은 묶어서 사람에게 묻는다.

### 역할을 나눠 돌릴 때 (기획 · 구현 · 검증 에이전트)

| 역할 | 맡는 단계 | 받는 것 | 내놓는 것 |
|---|---|---|---|
| 기획 | 설계 | 요구사항, 이전 결정 | 작업 목록과 수용 기준, 결정 초안 |
| 구현 | 구현 | 작업 하나와 수용 기준 | 테스트와 코드 변경 |
| 검증 | 검증, 리뷰 반영의 triage | 변경된 코드와 문서, 수용 기준 | 리버스 엔지니어링 검증 결과(판정·근거·재현 결과) |

- 리버스 엔지니어링은 **검증 역할**이 맡는다. 구현한 쪽이 스스로 채점하지 않도록, 검증 역할은 구현 쪽의 설명이 아니라 결과물(코드, 문서, 실행 결과)만 보고 판단한다.
- 역할끼리 서로를 부르지 않는다. 순서를 정하고 다음 역할에 넘기는 것은 사람(또는 사람이 시작한 명령)이다.
- 커밋·푸시는 어느 역할도 요청 없이 하지 않는다.

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
