#!/usr/bin/env python3
"""Observe Hermes/Nous rate limits from agent.log; does not control the TUI."""
from __future__ import annotations

import argparse
import re
import sqlite3
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

DEFAULT_LOG = Path.home() / ".hermes" / "logs" / "agent.log"
DEFAULT_DB = Path.cwd() / ".hermes-rate-limits.sqlite3"

TS = r"(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3})"
API_FAIL_RE = re.compile(
    TS + r".*?API call failed \(attempt (?P<attempt>\d+)/(?:\d+)\).*?"
    r"thread=(?P<thread>\S+).*?provider=(?P<provider>\S+).*?"
    r"model=(?P<model>\S+).*?summary=(?P<summary>.*)$"
)
RETRY_RE = re.compile(
    TS + r".*?Retrying API call in (?P<wait>[\d.]+)s \(attempt (?P<attempt>\d+)/(?:\d+)\).*?"
    r"thread=(?P<thread>\S+).*?provider=(?P<provider>\S+).*?"
    r"model=(?P<model>\S+).*?error=(?P<error>.*)$"
)


def parse_ts(value: str) -> datetime:
    return datetime.strptime(value, "%Y-%m-%d %H:%M:%S,%f")


def classify(summary: str) -> str:
    s = summary.lower()
    if "fair-share rate limit" in s or "reason': 'rate_limited'" in s or '"reason": "rate_limited"' in s:
        return "fair_share"
    if "temporarily at capacity upstream" in s or "not your api key's rate limit" in s:
        return "upstream_capacity"
    return "other_429"


def connect(db_path: Path) -> sqlite3.Connection:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(db_path)
    db.execute("""CREATE TABLE IF NOT EXISTS events (
        event_key TEXT PRIMARY KEY,
        occurred_at TEXT NOT NULL,
        provider TEXT NOT NULL,
        model TEXT NOT NULL,
        event_type TEXT NOT NULL,
        attempt INTEGER NOT NULL,
        thread TEXT NOT NULL,
        retry_after REAL,
        summary TEXT NOT NULL
    )""")
    db.commit()
    return db


def scan(log_path: Path, db: sqlite3.Connection) -> int:
    if not log_path.is_file():
        raise FileNotFoundError(f"Hermes log not found: {log_path}")
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    retries = []
    failures = []
    for line in lines:
        m = RETRY_RE.search(line)
        if m and m.group("provider") == "nous":
            retries.append((parse_ts(m.group("ts")), m.groupdict()))
            continue
        m = API_FAIL_RE.search(line)
        if m and m.group("provider") == "nous":
            failures.append((parse_ts(m.group("ts")), m.groupdict()))

    inserted = 0
    for failed_at, f in failures:
        # Match the retry record for the same TUI request/attempt, shortly after failure.
        candidates = [
            (retry_at, r) for retry_at, r in retries
            if r["thread"] == f["thread"]
            and r["attempt"] == f["attempt"]
            and r["model"] == f["model"]
            and 0 <= (retry_at - failed_at).total_seconds() <= 15
        ]
        retry_after = None
        if candidates:
            retry_at, r = min(candidates, key=lambda item: abs((item[0] - failed_at).total_seconds()))
            retry_after = float(r["wait"])
            # Retry error text often contains the detailed upstream refusal.
            detail = r["error"]
        else:
            detail = f["summary"]
        event_type = classify(f["summary"] + " " + detail)
        occurred = failed_at.isoformat(sep=" ", timespec="milliseconds")
        key = f"{occurred}|{f['thread']}|{f['model']}|{f['attempt']}"
        cur = db.execute(
            "INSERT OR IGNORE INTO events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (key, occurred, f["provider"], f["model"], event_type,
             int(f["attempt"]), f["thread"], retry_after, f["summary"][:2000]),
        )
        inserted += cur.rowcount
    db.commit()
    return inserted


def report(db: sqlite3.Connection) -> None:
    rows = db.execute(
        "SELECT occurred_at, model, event_type, retry_after FROM events ORDER BY occurred_at"
    ).fetchall()
    if not rows:
        print("저장된 Nous 429 이벤트가 없습니다. 먼저 scan을 실행하세요.")
        return
    groups: dict[tuple[str, str], list[tuple[datetime, float | None]]] = {}
    for ts, model, kind, wait in rows:
        groups.setdefault((model, kind), []).append((datetime.fromisoformat(ts), wait))

    print(f"총 이벤트: {len(rows)}건\n")
    for (model, kind), items in sorted(groups.items()):
        waits = [w for _, w in items if w is not None]
        intervals = [
            (items[i][0] - items[i - 1][0]).total_seconds()
            for i in range(1, len(items))
        ]
        print(f"[{model}] / {kind}")
        print(f"  발생 횟수: {len(items)}")
        print(f"  최초/최근: {items[0][0]} / {items[-1][0]}")
        if waits:
            print("  retry_after 초: "
                  f"최소 {min(waits):.0f}, 중앙값 {statistics.median(waits):.0f}, "
                  f"최대 {max(waits):.0f}")
        else:
            print("  retry_after: 로그에서 추출하지 못함")
        if intervals:
            print("  이벤트 간격 초: "
                  f"최소 {min(intervals):.1f}, 중앙값 {statistics.median(intervals):.1f}, "
                  f"평균 {statistics.mean(intervals):.1f}")
        else:
            print("  이벤트 간격: 이벤트가 1건뿐이라 계산할 수 없음")
        print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("scan", "report", "watch"))
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--interval", type=float, default=10.0,
                        help="watch 폴링 간격(초), 기본 10초")
    args = parser.parse_args()

    db = connect(args.db)
    try:
        if args.command == "scan":
            count = scan(args.log, db)
            print(f"새 이벤트 {count}건 저장: {args.db}")
        elif args.command == "report":
            report(db)
        else:
            print(f"로그 감시 시작: {args.log} (종료: Ctrl+C)")
            try:
                while True:
                    count = scan(args.log, db)
                    if count:
                        print(f"[{datetime.now().isoformat(timespec='seconds')}] 새 이벤트 {count}건")
                        report(db)
                    time.sleep(max(2.0, args.interval))
            except KeyboardInterrupt:
                print("\n감시 종료")
    finally:
        db.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
