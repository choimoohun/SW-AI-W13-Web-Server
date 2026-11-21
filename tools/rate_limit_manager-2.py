#!/usr/bin/env python3
"""Hermes/Nous Rate Limit Manager.

Tracks observed Nous rate-limit events and provides a local, manual request gate.
It does NOT intercept or disable requests made inside the Hermes TUI.
"""
from __future__ import annotations

import argparse
import re
import shlex
import sqlite3
import statistics
import sys
import time
from datetime import date, datetime, timedelta
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
    if ("fair-share rate limit" in s or "rate_limited" in s
            or "reached this model's current fair-share" in s):
        return "fair_share"
    if "temporarily at capacity upstream" in s or "not your api key's rate limit" in s:
        return "upstream_capacity"
    return "other_429"


def connect(db_path: Path) -> sqlite3.Connection:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(db_path)
    db.row_factory = sqlite3.Row
    db.executescript("""
    CREATE TABLE IF NOT EXISTS events (
        event_key TEXT PRIMARY KEY, occurred_at TEXT NOT NULL,
        provider TEXT NOT NULL, model TEXT NOT NULL, event_type TEXT NOT NULL,
        attempt INTEGER NOT NULL, thread TEXT NOT NULL,
        retry_after REAL, summary TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS daily_plans (
        plan_date TEXT PRIMARY KEY, daily_budget INTEGER NOT NULL,
        paused INTEGER NOT NULL DEFAULT 0, pause_reason TEXT NOT NULL DEFAULT ''
    );
    CREATE TABLE IF NOT EXISTS tasks (
        plan_date TEXT NOT NULL, task_id TEXT NOT NULL, task_name TEXT NOT NULL,
        request_budget INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'todo',
        PRIMARY KEY (plan_date, task_id)
    );
    CREATE TABLE IF NOT EXISTS reservations (
        id INTEGER PRIMARY KEY AUTOINCREMENT, plan_date TEXT NOT NULL,
        task_id TEXT NOT NULL, reserved_at TEXT NOT NULL,
        note TEXT NOT NULL DEFAULT ''
    );
    CREATE TABLE IF NOT EXISTS log_cursors (
        log_path TEXT PRIMARY KEY, inode INTEGER NOT NULL, byte_offset INTEGER NOT NULL
    );
    """)
    db.commit()
    return db


def scan(log_path: Path, db: sqlite3.Connection) -> int:
    """Parse only bytes appended since the previous scan (handles log rotation)."""
    if not log_path.is_file():
        raise FileNotFoundError(f"Hermes log not found: {log_path}")
    stat = log_path.stat()
    cursor = db.execute("SELECT inode,byte_offset FROM log_cursors WHERE log_path=?",
                        (str(log_path.resolve()),)).fetchone()
    offset = int(cursor["byte_offset"]) if cursor and cursor["inode"] == stat.st_ino else 0
    with log_path.open("rb") as fh:
        if offset > stat.st_size:
            offset = 0
        fh.seek(offset)
        chunk = fh.read()
    # Keep an incomplete final line for the next poll instead of parsing half a record.
    last_newline = chunk.rfind(b"\n")
    if last_newline < 0:
        complete_bytes = b""
        new_offset = offset
    else:
        complete_bytes = chunk[:last_newline + 1]
        new_offset = offset + last_newline + 1
    lines = complete_bytes.decode("utf-8", errors="replace").splitlines()

    retries: list[tuple[datetime, dict[str, str]]] = []
    failures: list[tuple[datetime, dict[str, str]]] = []
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
        candidates = [
            (retry_at, r) for retry_at, r in retries
            if r["thread"] == f["thread"] and r["attempt"] == f["attempt"]
            and r["model"] == f["model"]
            and 0 <= (retry_at - failed_at).total_seconds() <= 15
        ]
        retry_after = None
        detail = f["summary"]
        if candidates:
            _, r = min(candidates, key=lambda item: abs((item[0] - failed_at).total_seconds()))
            retry_after = float(r["wait"])
            detail = r["error"]
        event_type = classify(f["summary"] + " " + detail)
        occurred = failed_at.isoformat(sep=" ", timespec="milliseconds")
        key = f"{occurred}|{f['thread']}|{f['model']}|{f['attempt']}"
        cur = db.execute(
            "INSERT OR IGNORE INTO events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (key, occurred, f["provider"], f["model"], event_type,
             int(f["attempt"]), f["thread"], retry_after, f["summary"][:2000]),
        )
        inserted += cur.rowcount
    db.execute("INSERT INTO log_cursors(log_path,inode,byte_offset) VALUES(?,?,?) "
               "ON CONFLICT(log_path) DO UPDATE SET inode=excluded.inode,byte_offset=excluded.byte_offset",
               (str(log_path.resolve()), stat.st_ino, new_offset))
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
    for row in rows:
        groups.setdefault((row["model"], row["event_type"]), []).append(
            (datetime.fromisoformat(row["occurred_at"]), row["retry_after"])
        )
    print(f"총 이벤트: {len(rows)}건\n")
    for (model, kind), items in sorted(groups.items()):
        waits = [w for _, w in items if w is not None]
        intervals = [(items[i][0] - items[i - 1][0]).total_seconds()
                     for i in range(1, len(items))]
        print(f"[{model}] / {kind}\n  발생 횟수: {len(items)}")
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


def today() -> str:
    return date.today().isoformat()


def plan_create(db: sqlite3.Connection, budget: int, plan_date: str) -> None:
    if budget < 1:
        raise ValueError("일일 요청 예산은 1 이상이어야 합니다.")
    db.execute("INSERT INTO daily_plans(plan_date,daily_budget) VALUES(?,?) "
               "ON CONFLICT(plan_date) DO UPDATE SET daily_budget=excluded.daily_budget",
               (plan_date, budget))
    db.commit()
    print(f"계획 생성/갱신: {plan_date}, 자체 요청 예산 {budget}회")
    print("주의: 실제 Nous 무료 한도나 서버 잔여량이 아니라 사용자가 정한 내부 예산입니다.")


def task_add(db: sqlite3.Connection, plan_date: str, task_id: str,
             name: str, budget: int) -> None:
    if budget < 1:
        raise ValueError("작업별 요청 예산은 1 이상이어야 합니다.")
    if not db.execute("SELECT 1 FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone():
        raise ValueError(f"{plan_date} 계획이 없습니다. 먼저 plan-create를 실행하세요.")
    db.execute("INSERT INTO tasks(plan_date,task_id,task_name,request_budget) VALUES(?,?,?,?) "
               "ON CONFLICT(plan_date,task_id) DO UPDATE SET "
               "task_name=excluded.task_name, request_budget=excluded.request_budget",
               (plan_date, task_id, name, budget))
    db.commit()
    total_assigned = db.execute("SELECT COALESCE(SUM(request_budget),0) FROM tasks WHERE plan_date=?",
                                (plan_date,)).fetchone()[0]
    daily_budget = db.execute("SELECT daily_budget FROM daily_plans WHERE plan_date=?",
                              (plan_date,)).fetchone()[0]
    print(f"작업 등록: {task_id} / {name} / 예산 {budget}회")
    if total_assigned > daily_budget:
        print(f"주의: 작업별 예산 합계 {total_assigned}회가 일일 예산 {daily_budget}회를 초과합니다.")


def cancel_reservation(db: sqlite3.Connection, task_id: str, plan_date: str) -> None:
    row = db.execute("SELECT id FROM reservations WHERE plan_date=? AND task_id=? ORDER BY id DESC LIMIT 1",
                     (plan_date, task_id)).fetchone()
    if not row:
        print("취소할 예약이 없습니다.")
        return
    db.execute("DELETE FROM reservations WHERE id=?", (row["id"],))
    db.commit()
    print(f"마지막 요청 예약 1건 취소: {task_id}")


def plan_status(db: sqlite3.Connection, plan_date: str) -> None:
    plan = db.execute("SELECT * FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone()
    if not plan:
        print(f"{plan_date} 계획이 없습니다. plan-create로 생성하세요.")
        return
    used = db.execute("SELECT COUNT(*) FROM reservations WHERE plan_date=?", (plan_date,)).fetchone()[0]
    print(f"[{plan_date}] 일일 요청 예산: {plan['daily_budget']} / 예약 사용: {used} / 잔여: {max(0, plan['daily_budget']-used)}")
    print(f"상태: {'PAUSED - ' + plan['pause_reason'] if plan['paused'] else 'ACTIVE'}")
    tasks = db.execute("""SELECT t.*, COUNT(r.id) AS used FROM tasks t
        LEFT JOIN reservations r ON r.plan_date=t.plan_date AND r.task_id=t.task_id
        WHERE t.plan_date=? GROUP BY t.plan_date,t.task_id ORDER BY t.rowid""", (plan_date,)).fetchall()
    if not tasks:
        print("등록된 작업이 없습니다.")
    for t in tasks:
        remaining = max(0, t["request_budget"] - t["used"])
        print(f"- {t['task_id']}: {t['task_name']} | {t['status']} | {t['used']}/{t['request_budget']} | 잔여 {remaining}")


def active_cooldown(db: sqlite3.Connection, now: datetime) -> tuple[float, str] | None:
    row = db.execute("""SELECT occurred_at,retry_after,event_type,model FROM events
        WHERE event_type='fair_share' ORDER BY occurred_at DESC LIMIT 1""").fetchone()
    if not row or row["retry_after"] is None:
        return None
    event_time = datetime.fromisoformat(row["occurred_at"])
    until = event_time + timedelta(seconds=float(row["retry_after"]))
    remaining = (until - now).total_seconds()
    if remaining > 0:
        return remaining, row["model"]
    return None


def request_gate(db: sqlite3.Connection, task_id: str, plan_date: str, note: str = "") -> int:
    now = datetime.now()
    plan = db.execute("SELECT * FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone()
    if not plan:
        print("BLOCKED: 오늘 계획이 없습니다. 먼저 plan-create를 실행하세요.")
        return 2
    if plan["paused"]:
        print(f"BLOCKED: 계획이 일시 중지되었습니다. 사유: {plan['pause_reason']}")
        return 2
    cooldown = active_cooldown(db, now)
    if cooldown:
        seconds, model = cooldown
        print(f"BLOCKED: {model} Fair-share cooldown 진행 중. 남은 시간 약 {seconds:.0f}초")
        return 2
    task = db.execute("SELECT * FROM tasks WHERE plan_date=? AND task_id=?",
                      (plan_date, task_id)).fetchone()
    if not task:
        print(f"BLOCKED: 작업 ID '{task_id}'가 계획에 없습니다.")
        return 2
    total_used = db.execute("SELECT COUNT(*) FROM reservations WHERE plan_date=?", (plan_date,)).fetchone()[0]
    task_used = db.execute("SELECT COUNT(*) FROM reservations WHERE plan_date=? AND task_id=?",
                           (plan_date, task_id)).fetchone()[0]
    if total_used >= plan["daily_budget"]:
        print("BLOCKED: 일일 자체 요청 예산을 모두 사용했습니다.")
        return 2
    if task_used >= task["request_budget"]:
        print("BLOCKED: 해당 작업의 자체 요청 예산을 모두 사용했습니다.")
        return 2
    db.execute("INSERT INTO reservations(plan_date,task_id,reserved_at,note) VALUES(?,?,?,?)",
               (plan_date, task_id, now.isoformat(timespec="seconds"), note))
    db.commit()
    print("ALLOW: 로컬 예산 및 기록된 cooldown 검사 통과")
    print(f"작업: {task['task_name']} ({task_id})")
    print(f"오늘 예약 사용량: {total_used + 1}/{plan['daily_budget']}")
    print("이제 Hermes TUI에 요청을 입력할 수 있습니다.")
    print("주의: 이는 로컬 사전 검사이며 Nous 서버의 실시간 잔여 한도를 보증하지 않습니다.")
    return 0


def set_pause(db: sqlite3.Connection, plan_date: str, paused: bool, reason: str) -> None:
    cur = db.execute("UPDATE daily_plans SET paused=?, pause_reason=? WHERE plan_date=?",
                     (int(paused), reason if paused else "", plan_date))
    db.commit()
    if not cur.rowcount:
        print("계획이 없습니다. 먼저 plan-create를 실행하세요.")
    else:
        print("요청 게이트 일시 중지" if paused else "요청 게이트 재개")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--log", type=Path, default=DEFAULT_LOG)
    p.add_argument("--db", type=Path, default=DEFAULT_DB)
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("scan")
    sub.add_parser("report")
    w = sub.add_parser("watch")
    w.add_argument("--interval", type=float, default=10.0)
    pc = sub.add_parser("plan-create")
    pc.add_argument("--budget", type=int, required=True)
    pc.add_argument("--date", default=today())
    ta = sub.add_parser("task-add")
    ta.add_argument("--date", default=today())
    ta.add_argument("--id", required=True, help="짧은 작업 ID. 예: http-parser")
    ta.add_argument("--name", required=True)
    ta.add_argument("--budget", type=int, required=True)
    ps = sub.add_parser("plan")
    ps.add_argument("action", choices=("status",))
    ps.add_argument("--date", default=today())
    rq = sub.add_parser("request")
    rq.add_argument("--task", required=True, help="task-add에서 등록한 작업 ID")
    rq.add_argument("--date", default=today())
    rq.add_argument("--note", default="")
    pa = sub.add_parser("pause")
    pa.add_argument("--date", default=today())
    pa.add_argument("--reason", default="사용자 일시 중지")
    re_ = sub.add_parser("resume")
    re_.add_argument("--date", default=today())
    ca = sub.add_parser("cancel")
    ca.add_argument("--task", required=True)
    ca.add_argument("--date", default=today())
    return p


def main() -> int:
    args = build_parser().parse_args()
    db = connect(args.db)
    try:
        if args.command == "scan":
            count = scan(args.log, db)
            print(f"새 이벤트 {count}건 저장")
        elif args.command == "report":
            report(db)
        elif args.command == "watch":
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
        elif args.command == "plan-create":
            plan_create(db, args.budget, args.date)
        elif args.command == "task-add":
            task_add(db, args.date, args.id, args.name, args.budget)
        elif args.command == "plan":
            plan_status(db, args.date)
        elif args.command == "request":
            # Refresh log-derived cooldown state before evaluating the gate.
            if args.log.is_file():
                scan(args.log, db)
            return request_gate(db, args.task, args.date, args.note)
        elif args.command == "pause":
            set_pause(db, args.date, True, args.reason)
        elif args.command == "resume":
            set_pause(db, args.date, False, "")
        elif args.command == "cancel":
            cancel_reservation(db, args.task, args.date)
    except (OSError, sqlite3.Error, ValueError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1
    finally:
        db.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
