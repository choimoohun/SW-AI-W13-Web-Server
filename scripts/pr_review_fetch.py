#!/usr/bin/env python3
"""PR 리뷰 코멘트를 모아 검토 체크리스트 초안(markdown)을 만든다.

사용:
    python3 scripts/pr_review_fetch.py 12
    python3 scripts/pr_review_fetch.py 12 --repo choimoohun/SW-AI-W13-Web-Server

결과: docs/records/pr_reviews/PR-12.md
  - 리뷰 항목마다 R1, R2 ... 번호를 붙이고
  - 에이전트가 채울 칸(판정·근거·제안 수정·검증 명령)과
  - 사람이 체크할 칸([ ] 반영 승인)을 만든다.

이미 파일이 있으면 덮어쓰지 않고 새 코멘트만 뒤에 붙인다
(사람이 체크한 내용을 지우지 않기 위해).

필요: gh CLI 로그인(gh auth login). REST API만 사용한다.
"""
import argparse
import json
import os
import re
import subprocess
import sys

OUT_DIR = os.path.join("docs", "records", "pr_reviews")
BOT_NOISE = (
    "<!-- This is an auto-generated comment: summarize by coderabbit",
    "<!-- walkthrough_start -->",
)


def gh_api(path):
    """gh api 결과를 JSON으로. 페이지가 여러 개면 모두 합친다."""
    out = subprocess.run(
        ["gh", "api", "--paginate", path],
        check=True, capture_output=True, text=True,
    ).stdout
    # --paginate는 배열을 페이지마다 이어 붙여 '][' 형태가 될 수 있다.
    out = re.sub(r"\]\s*\[", ",", out.strip())
    return json.loads(out) if out else []


def detect_repo():
    out = subprocess.run(
        ["gh", "repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"],
        capture_output=True, text=True,
    )
    if out.returncode != 0 or not out.stdout.strip():
        sys.exit("저장소를 찾지 못했습니다. --repo owner/name 을 넘겨 주세요.")
    return out.stdout.strip()


def clean(body):
    """CodeRabbit 접힘 블록·HTML 주석을 걷어 내고 본문만 남긴다."""
    body = re.sub(r"<!--.*?-->", "", body or "", flags=re.S)
    body = re.sub(r"<details>.*?</details>", "(접힌 상세 생략 — 원문 링크 참고)", body, flags=re.S)
    return body.strip()


def quote(text, limit=1200):
    text = text if len(text) <= limit else text[:limit] + " …(생략, 원문 링크 참고)"
    return "\n".join("> " + line if line else ">" for line in text.splitlines())


def collect(repo, pr):
    items = []

    # 1) 코드 줄에 달린 리뷰 코멘트. 답글(in_reply_to_id)은 첫 코멘트 아래로 묶는다.
    inline = gh_api(f"repos/{repo}/pulls/{pr}/comments")
    threads = {}
    for c in inline:
        root = c.get("in_reply_to_id") or c["id"]
        threads.setdefault(root, []).append(c)
    for root, cs in threads.items():
        first = cs[0]
        line = first.get("line") or first.get("original_line")
        items.append({
            "key": f"inline-{root}",
            "where": f"{first.get('path')}:{line}" if line else first.get("path", "?"),
            "author": first["user"]["login"],
            "url": first.get("html_url", ""),
            "body": clean(first.get("body")),
            "replies": [f"{c['user']['login']}: {clean(c.get('body'))}" for c in cs[1:]],
        })

    # 2) 리뷰 본문(Approve / Request changes와 함께 남긴 총평)
    for r in gh_api(f"repos/{repo}/pulls/{pr}/reviews"):
        body = clean(r.get("body"))
        if not body:
            continue
        items.append({
            "key": f"review-{r['id']}",
            "where": f"리뷰 총평 ({r.get('state', '').lower()})",
            "author": r["user"]["login"],
            "url": r.get("html_url", ""),
            "body": body,
            "replies": [],
        })

    # 3) PR 대화 탭 코멘트. CodeRabbit 요약(walkthrough)은 검토 항목이 아니라서 뺀다.
    for c in gh_api(f"repos/{repo}/issues/{pr}/comments"):
        raw = c.get("body") or ""
        if any(m in raw for m in BOT_NOISE):
            continue
        body = clean(raw)
        if not body or body.startswith("@coderabbitai"):
            continue
        items.append({
            "key": f"issue-{c['id']}",
            "where": "PR 대화",
            "author": c["user"]["login"],
            "url": c.get("html_url", ""),
            "body": body,
            "replies": [],
        })
    return items


def render_item(n, it):
    replies = ""
    if it["replies"]:
        replies = "\n답글:\n" + "\n".join(quote(r, 400) for r in it["replies"]) + "\n"
    return f"""
### R{n} · {it['where']} · {it['author']}
<!-- key: {it['key']} -->

{quote(it['body'])}
{replies}
원문: {it['url']}

- 판정(에이전트):
- 지금 작업 브랜치에서(에이전트):
- 근거(에이전트):
- 제안 수정(에이전트):
- 검증 명령(에이전트):
- 위험도(에이전트):
- [ ] 반영 승인 (사람이 체크)
- 메모(사람):
- 결과(에이전트):
- 답글 초안(에이전트):
"""


HEADER = """# PR #{pr} 리뷰 검토 체크리스트

저장소: {repo}

진행 순서:
1. `pr-review-loop` 스킬의 triage 단계가 각 항목의 '(에이전트)' 칸을 채운다. 코드는 고치지 않는다.
2. 사람이 읽고, 반영할 항목만 `- [x] 반영 승인`으로 체크한다. 필요하면 '메모(사람)'에 방향을 적는다.
3. apply 단계가 체크된 항목만 고치고, 검증 명령을 실제로 돌려 '결과(에이전트)'를 채운다.

판정 값: 타당 / 부분 타당 / 해당 없음 / 판단 보류(사람 결정 필요)
작업 브랜치 상태 값: 남아 있음 / 이미 해결됨 / 해당 코드 없음
(리뷰는 PR 당시 커밋을 본 것이라, 지금 작업 브랜치에서 아직 유효한지 따로 적는다)
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("pr", type=int, help="PR 번호")
    ap.add_argument("--repo", help="owner/name (생략하면 현재 저장소)")
    args = ap.parse_args()

    repo = args.repo or detect_repo()
    items = collect(repo, args.pr)

    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, f"PR-{args.pr}.md")

    existing = ""
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            existing = f.read()
    seen = set(re.findall(r"<!-- key: (\S+) -->", existing))
    next_n = len(re.findall(r"^### R\d+", existing, flags=re.M)) + 1

    new = [it for it in items if it["key"] not in seen]
    chunks = [render_item(next_n + i, it) for i, it in enumerate(new)]

    with open(path, "w", encoding="utf-8") as f:
        f.write(existing or HEADER.format(pr=args.pr, repo=repo))
        f.write("".join(chunks))

    print(f"{path}: 새 항목 {len(new)}개 (전체 {len(seen) + len(new)}개)")


if __name__ == "__main__":
    main()
