# STEP4 / STEP5 PR 분리 머지 계획

## 현재 구조

- `eungi-step4`와 `eungi-step5`는 둘 다 `main(95f5683)`에서 갈라졌습니다.
- `eungi-step5`에는 `eungi-step4`의 마지막 커밋 `25dc3fd`가 없습니다.
- `#19`: `eungi-step4` → `eungi-main` (OPEN, MERGEABLE, 리뷰 댓글 27개)
- `#22`: `eungi-step5` → `eungi-step4` (OPEN, MERGEABLE, 리뷰 댓글 25개)

## 순서

1. `#19` — 리뷰 반영 → `eungi-step4`에 커밋·푸시 → `eungi-main`에 머지.
   - `eungi-step4`는 `eungi-main`보다 1커밋 뒤처져 있으므로, 머지 전에
     `git rebase origin/eungi-main`을 하면 깨끗합니다.
2. `eungi-step5` 최신화 — `#19` 머지 **이후**에 실행합니다.
   ```
   git checkout eungi-step5
   git rebase origin/eungi-main
   git push --force-with-lease origin eungi-step5
   ```
3. `#22` base 변경 — head는 API로 못 바꾸지만 base는 바꿀 수 있습니다.
   ```
   gh api -X PATCH repos/{owner}/{repo}/pulls/22 -f base=eungi-main
   ```
4. `#22` 리뷰 반영 → `eungi-main`에 머지.

이렇게 하면 `#19`는 STEP4만, `#22`는 STEP5만으로 따로 리뷰·수정·머지됩니다.

## 미결정

- 머지 방식: `--squash` / `--merge` / `--rebase` 중 선택. 팀 규칙 미정
  (`docs/TEAM_RULES.md`에 명시 필요).
- 리뷰 댓글 27개·25개를 반영할지, 반영한다면 어떤 것을 반영할지 아직 정하지 않았다.

## 메모

- PR head 브랜치는 GitHub REST API로 변경할 수 없다. 범위를 바꾸려면 새 브랜치에서
  PR을 열고 기존 PR을 닫아야 한다. base는 `PATCH /pulls/{n}`으로 변경 가능하다.
