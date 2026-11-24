# 에이전트 스킬 (`docs/agent/skills/`)

이 폴더의 스킬은 두 종류입니다. 루트 `AGENTS.md`가 상황별로 이 파일들을 가리키므로, Hermes는 AGENTS.md를 읽고 필요한 스킬을 찾아 씁니다.

| 스킬 | 출처 | 언제 쓰나 |
|---|---|---|
| `pr-review-loop` | 이 레포에서 직접 작성 | PR 리뷰를 리버스 엔지니어링으로 확인 → 사람 체크 → 체크한 것만 반영 |
| `code-review-and-quality` | agent-skills | PR을 올리기 **전** 스스로 점검. 결과는 `docs/records/pr_reviews/` 체크리스트 형식으로 남기고 같은 방식으로 triage |
| `test-driven-development` | agent-skills | 동작이 바뀌는 수정. 실패하는 테스트를 먼저 만들고 실패를 확인한 뒤 고침 |
| `debugging-and-error-recovery` | agent-skills | 테스트가 실패하거나 원인을 모를 때. 재현 → 원인 → 회귀 테스트 |
| `documentation-and-adrs` | agent-skills | 방향 결정이 생겼을 때. `docs/DECISIONS.md`·`docs/decisions/`에 기록 |

## agent-skills 출처

- 저장소: https://github.com/addyosmani/agent-skills (커밋 `8c99fa3`, 2026-11-18 기준 복사)
- 라이선스: MIT, Copyright (c) 2025 Addy Osmani — 전문은 [LICENSE-agent-skills](LICENSE-agent-skills)
- 원본을 수정하지 않고 그대로 복사했습니다. 스킬이 참조하는 체크리스트는 `docs/agent/references/`에 있습니다.
- 이 레포에 맞춘 규칙(명령, 금지 사항, 문서 위치)은 루트 `AGENTS.md`가 우선합니다.
  예: 스킬이 커밋을 권해도 `AGENTS.md`의 "요청 없는 커밋 금지"를 따릅니다. 테스트 명령은 `make test`, `make integration`입니다.
