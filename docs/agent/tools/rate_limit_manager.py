#하루 작업 범위를 API 리밋에 맞춰 나누고 현재 상태를 확인하는 로컬 스크립트
#!/usr/bin/env python3
"""Automated local rate-limit watcher and gate for Hermes TUI (Nous, Qwen, etc.)."""
from __future__ import annotations

import argparse
import re
import sqlite3
import statistics
import sys
import time
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

DEFAULT_LOG = Path.home() / ".hermes" / "logs" / "agent.log"
DEFAULT_DB = Path.home() / ".hermes" / "rate-limit-manager.sqlite3"

DEFAULT_BUDGET = 22
DEFAULT_RESERVE = 2
DEFAULT_WINDOW_SECONDS = 600
DEFAULT_WINDOW_LIMIT = 2
DEFAULT_MIN_INTERVAL = 120.0
DEFAULT_MIN_COOLDOWN = 60.0

TS_RE = re.compile(r"(?P<ts>\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:[,.]\d{3,6})?)")
RETRY_RE = re.compile(r"Retrying API call in (?P<wait>[\d.]+)s \(attempt (?P<attempt>\d+)/\d+\)")
FAIL_RE = re.compile(r"API call failed \(attempt (?P<attempt>\d+)/\d+\)")
FIELD_PROVIDER_RE = re.compile(r"provider=(?P<val>\S+)")
FIELD_MODEL_RE = re.compile(r"model=(?P<val>\S+)")
FIELD_THREAD_RE = re.compile(r"thread=(?P<val>\S+)")
WAIT_FALLBACK_RE = re.compile(r"(?:retry[-_ ]?after|wait|in)\s*[=:]?\s*(?P<wait>[\d.]+)\s*s", re.I)
RATE_HINT_RE = re.compile(
    r"(?:429|rate[_ -]?limit|fair[-_ ]?share|too many requests|at capacity|quota|throttl)",
    re.I,
)


def parse_ts(value: str) -> datetime:
    normalized = value.replace("T", " ").replace(",", ".")
    for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S"):
        try:
            return datetime.strptime(normalized, fmt)
        except ValueError:
            continue
    raise ValueError(f"Unsupported timestamp: {value}")


def local_utc_offset_seconds() -> float:
    now_local = datetime.now().astimezone()
    offset = now_local.utcoffset()
    return offset.total_seconds() if offset else 0.0


def to_local_dt(ts: datetime, assume_utc: bool) -> datetime:
    if not assume_utc:
        return ts
    return ts.replace(tzinfo=timezone.utc).astimezone().replace(tzinfo=None)


def infer_provider_and_model(line: str) -> tuple[str, str]:
    p_match = FIELD_PROVIDER_RE.search(line)
    m_match = FIELD_MODEL_RE.search(line)
    provider = p_match.group("val").strip(",;") if p_match else ""
    model = m_match.group("val").strip(",;") if m_match else ""

    lower = line.lower()
    if not provider:
        if "qwen" in lower:
            provider = "qwen"
        elif "nous" in lower or "hermes" in lower:
            provider = "nous"
        elif "openrouter" in lower:
            provider = "openrouter"
        elif "dashscope" in lower or "alibaba" in lower:
            provider = "dashscope"
        elif "openai" in lower:
            provider = "openai"
        else:
            provider = "unknown"

    if not model:
        qwen_m = re.search(r"(qwen[\w./:-]*)", lower)
        nous_m = re.search(r"(hermes[\w./:-]*|nous[\w./:-]*)", lower)
        if qwen_m:
            model = qwen_m.group(1)
        elif nous_m:
            model = nous_m.group(1)
        else:
            model = "default"

    return provider.lower(), model


def classify(message: str) -> str:
    value = message.lower()
    if ("fair-share" in value or "fair_share" in value
            or "rate_limited" in value or "reached this model's current" in value):
        return "fair_share"
    if ("temporarily at capacity" in value or "not your api key's rate limit" in value):
        return "upstream_capacity"
    if "quota" in value or "insufficient" in value:
        return "quota"
    return "other_429"


def connect(db_path: Path) -> sqlite3.Connection:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(db_path, timeout=15)
    db.row_factory = sqlite3.Row
    db.execute("PRAGMA busy_timeout=15000")
    db.executescript("""
    CREATE TABLE IF NOT EXISTS events (
        event_key TEXT PRIMARY KEY, occurred_at TEXT NOT NULL,
        provider TEXT NOT NULL, model TEXT NOT NULL, event_type TEXT NOT NULL,
        attempt INTEGER NOT NULL, thread TEXT NOT NULL,
        retry_after REAL, summary TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS daily_plans (
        plan_date TEXT PRIMARY KEY, daily_budget INTEGER NOT NULL,
        paused INTEGER NOT NULL DEFAULT 0, pause_reason TEXT NOT NULL DEFAULT '',
        safety_reserve INTEGER NOT NULL DEFAULT 0,
        window_seconds INTEGER NOT NULL DEFAULT 0,
        window_limit INTEGER NOT NULL DEFAULT 0,
        min_interval_seconds REAL NOT NULL DEFAULT 0
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
    existing = {row["name"] for row in db.execute("PRAGMA table_info(daily_plans)")}
    migrations = {
        "safety_reserve": "INTEGER NOT NULL DEFAULT 0",
        "window_seconds": "INTEGER NOT NULL DEFAULT 0",
        "window_limit": "INTEGER NOT NULL DEFAULT 0",
        "min_interval_seconds": "REAL NOT NULL DEFAULT 0",
    }
    for name, declaration in migrations.items():
        if name not in existing:
            db.execute(f"ALTER TABLE daily_plans ADD COLUMN {name} {declaration}")
    db.commit()
    return db


def should_assume_utc(log_path: Path, lines: list[str], log_tz: str) -> bool:
    if log_tz == "utc":
        return True
    if log_tz == "local":
        return False
    offset = local_utc_offset_seconds()
    if abs(offset) < 60:
        return False
    for line in reversed(lines):
        m = TS_RE.match(line)
        if m:
            try:
                last_ts = parse_ts(m.group("ts"))
            except ValueError:
                continue
            mtime_dt = datetime.fromtimestamp(log_path.stat().st_mtime)
            diff = (mtime_dt - last_ts).total_seconds()
            return abs(diff - offset) < 600
    return False


def scan(log_path: Path, db: sqlite3.Connection, log_tz: str = "auto") -> int:
    if not log_path.is_file():
        return 0
    stat = log_path.stat()
    resolved = str(log_path.resolve())
    cursor = db.execute(
        "SELECT inode, byte_offset FROM log_cursors WHERE log_path=?", (resolved,)
    ).fetchone()
    offset = int(cursor["byte_offset"]) if cursor and cursor["inode"] == stat.st_ino else 0
    if offset > stat.st_size:
        offset = 0

    with log_path.open("rb") as handle:
        handle.seek(offset)
        chunk = handle.read()

    last_newline = chunk.rfind(b"\n")
    if last_newline < 0:
        return 0

    complete = chunk[:last_newline + 1]
    new_offset = offset + last_newline + 1
    lines = complete.decode("utf-8", errors="replace").splitlines()
    assume_utc = should_assume_utc(log_path, lines, log_tz)

    inserted = 0
    for line in lines:
        ts_match = TS_RE.match(line)
        if not ts_match:
            continue

        retry_m = RETRY_RE.search(line)
        fail_m = FAIL_RE.search(line)
        is_rate_line = RATE_HINT_RE.search(line) is not None
        if not retry_m and not fail_m and not is_rate_line:
            continue
        if fail_m and not is_rate_line:
            # Ignore non-rate-limit failures unless clearly 429/rate-related.
            continue

        occurred = to_local_dt(parse_ts(ts_match.group("ts")), assume_utc).isoformat(
            sep=" ", timespec="milliseconds"
        )
        provider, model = infer_provider_and_model(line)
        t_match = FIELD_THREAD_RE.search(line)
        thread = t_match.group("val").strip(",;") if t_match else "main"

        if retry_m:
            attempt = int(retry_m.group("attempt"))
            wait: float | None = float(retry_m.group("wait"))
            tag = "retry"
        elif fail_m:
            attempt = int(fail_m.group("attempt"))
            w_m = WAIT_FALLBACK_RE.search(line)
            wait = float(w_m.group("wait")) if w_m else None
            tag = "failure"
        else:
            attempt = 1
            w_m = WAIT_FALLBACK_RE.search(line)
            wait = float(w_m.group("wait")) if w_m else None
            tag = "rate_log"

        kind = classify(line)
        key = f"{occurred}|{thread}|{provider}|{model}|{attempt}|{tag}"
        cur = db.execute(
            """INSERT OR IGNORE INTO events
               (event_key,occurred_at,provider,model,event_type,attempt,thread,retry_after,summary)
               VALUES(?,?,?,?,?,?,?,?,?)""",
            (key, occurred, provider, model, kind, attempt, thread, wait, line[:2000]),
        )
        inserted += cur.rowcount

    db.execute(
        """INSERT INTO log_cursors(log_path,inode,byte_offset) VALUES(?,?,?)
           ON CONFLICT(log_path) DO UPDATE SET
           inode=excluded.inode, byte_offset=excluded.byte_offset""",
        (resolved, stat.st_ino, new_offset),
    )
    db.commit()
    return inserted


def today() -> str:
    return date.today().isoformat()


def ensure_today_plan(
    db: sqlite3.Connection,
    plan_date: str,
    budget: int = DEFAULT_BUDGET,
    reserve: int = DEFAULT_RESERVE,
    window_seconds: int = DEFAULT_WINDOW_SECONDS,
    window_limit: int = DEFAULT_WINDOW_LIMIT,
    min_interval: float = DEFAULT_MIN_INTERVAL,
) -> bool:
    exists = db.execute("SELECT 1 FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone()
    if exists:
        return False
    db.execute(
        """INSERT INTO daily_plans
           (plan_date,daily_budget,safety_reserve,window_seconds,window_limit,min_interval_seconds)
           VALUES(?,?,?,?,?,?)""",
        (plan_date, budget, reserve, window_seconds, window_limit, min_interval),
    )
    db.execute(
        """INSERT OR IGNORE INTO tasks(plan_date,task_id,task_name,request_budget,status)
           VALUES(?,?,?,?,?)""",
        (plan_date, "work", "오늘 작업(자동 생성)", max(1, budget - reserve), "in_progress"),
    )
    db.commit()
    return True


def active_cooldowns(
    db: sqlite3.Connection, now: datetime, min_cooldown: float = DEFAULT_MIN_COOLDOWN
) -> list[tuple[str, str, float, str]]:
    rows = db.execute(
        """SELECT provider, model, event_type, occurred_at, retry_after
           FROM events
           WHERE event_key IS NOT NULL OR rowid IS NOT NULL
           ORDER BY occurred_at DESC"""
    ).fetchall()
    seen: set[tuple[str, str]] = set()
    active: list[tuple[str, str, float, str]] = []
    for row in rows:
        pair = (row["provider"], row["model"])
        if pair in seen:
            continue
        seen.add(pair)
        event_time = datetime.fromisoformat(row["occurred_at"])
        cooldown_sec = max(float(row["retry_after"] or 0.0), min_cooldown)
        remaining = (event_time + timedelta(seconds=cooldown_sec) - now).total_seconds()
        if remaining > 0:
            active.append((row["provider"], row["model"], remaining, row["event_type"]))
    return active


def report(db: sqlite3.Connection) -> None:
    rows = db.execute(
        "SELECT occurred_at,provider,model,event_type,retry_after FROM events ORDER BY occurred_at"
    ).fetchall()
    if not rows:
        print("감지된 429/rate-limit 이벤트가 없습니다.")
        return
    groups: dict[tuple[str, str, str], list[tuple[datetime, float | None]]] = {}
    for row in rows:
        groups.setdefault((row["provider"], row["model"], row["event_type"]), []).append(
            (datetime.fromisoformat(row["occurred_at"]), row["retry_after"])
        )
    print(f"총 감지 이벤트: {len(rows)}건\n")
    for (provider, model, kind), items in sorted(groups.items()):
        waits = [w for _, w in items if w is not None]
        print(f"[{provider} / {model}] ({kind}) — {len(items)}회")
        print(f"  최초/최근: {items[0][0]} / {items[-1][0]}")
        if waits:
            print(
                f"  로그 대기초: 최소 {min(waits):.0f}s / 중앙값 {statistics.median(waits):.0f}s / 최대 {max(waits):.0f}s"
            )
        print()


def inspect_log(log_path: Path, tail_n: int = 15) -> None:
    if not log_path.is_file():
        print(f"로그 파일이 없습니다: {log_path}")
        return
    matched: list[str] = []
    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            low = line.lower()
            if any(k in low for k in ("429", "rate", "fair", "retrying", "qwen", "nous")):
                matched.append(line.rstrip("\n"))
    if not matched:
        print(f"{log_path} 안에 429/rate/qwen/nous 관련 로그가 아직 없습니다.")
        return
    print(f"=== {log_path} 관련 최근 {min(tail_n, len(matched))}줄 ===")
    for line in matched[-tail_n:]:
        print(line)


def plan_create(
    db: sqlite3.Connection, budget: int, plan_date: str, reserve: int,
    window_seconds: int, window_limit: int, min_interval: float
) -> None:
    if budget < 1:
        raise ValueError("일일 요청 예산은 1 이상이어야 합니다.")
    if reserve < 0 or reserve >= budget:
        raise ValueError("안전 예산은 0 이상, 일일 예산 미만이어야 합니다.")
    if window_seconds < 0 or window_limit < 0 or min_interval < 0:
        raise ValueError("시간 제한 값은 음수일 수 없습니다.")
    if bool(window_seconds) != bool(window_limit):
        raise ValueError("--window-seconds와 --window-limit은 함께 설정해야 합니다.")
    db.execute(
        """INSERT INTO daily_plans
           (plan_date,daily_budget,safety_reserve,window_seconds,window_limit,min_interval_seconds)
           VALUES(?,?,?,?,?,?)
           ON CONFLICT(plan_date) DO UPDATE SET
           daily_budget=excluded.daily_budget,
           safety_reserve=excluded.safety_reserve,
           window_seconds=excluded.window_seconds,
           window_limit=excluded.window_limit,
           min_interval_seconds=excluded.min_interval_seconds""",
        (plan_date, budget, reserve, window_seconds, window_limit, min_interval),
    )
    db.commit()
    print(f"계획 저장: {plan_date} (허용 {budget - reserve}회, 간격 {min_interval:g}초)")


def task_add(db: sqlite3.Connection, plan_date: str, task_id: str, name: str, budget: int) -> None:
    if budget < 1:
        raise ValueError("작업별 요청 예산은 1 이상이어야 합니다.")
    ensure_today_plan(db, plan_date)
    db.execute(
        """INSERT INTO tasks(plan_date,task_id,task_name,request_budget)
           VALUES(?,?,?,?)
           ON CONFLICT(plan_date,task_id) DO UPDATE SET
           task_name=excluded.task_name, request_budget=excluded.request_budget""",
        (plan_date, task_id, name, budget),
    )
    db.commit()
    print(f"작업 등록/갱신: {task_id} / {name} ({budget}회)")


def task_status(db: sqlite3.Connection, plan_date: str, task_id: str, status: str) -> None:
    cur = db.execute(
        "UPDATE tasks SET status=? WHERE plan_date=? AND task_id=?",
        (status, plan_date, task_id),
    )
    db.commit()
    print(f"작업 상태 변경: {task_id} -> {status}" if cur.rowcount else f"작업 없음: {task_id}")


def cancel_reservation(db: sqlite3.Connection, task_id: str, plan_date: str) -> None:
    row = db.execute(
        "SELECT id FROM reservations WHERE plan_date=? AND task_id=? ORDER BY id DESC LIMIT 1",
        (plan_date, task_id),
    ).fetchone()
    if not row:
        print("취소할 예약이 없습니다.")
        return
    db.execute("DELETE FROM reservations WHERE id=?", (row["id"],))
    db.commit()
    print(f"마지막 예약 1건 취소: {task_id}")


def plan_status(db: sqlite3.Connection, plan_date: str) -> None:
    ensure_today_plan(db, plan_date)
    plan = db.execute("SELECT * FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone()
    used = db.execute(
        "SELECT COUNT(*) FROM reservations WHERE plan_date=?", (plan_date,)
    ).fetchone()[0]
    allowed = plan["daily_budget"] - plan["safety_reserve"]
    print(f"[{plan_date}] 예약 사용: {used}/{allowed}회 (잔여 {max(0, allowed - used)}회) | "
          f"{'PAUSED' if plan['paused'] else 'ACTIVE'}")
    if plan["window_limit"]:
        cutoff = (datetime.now() - timedelta(seconds=plan["window_seconds"])).isoformat(timespec="seconds")
        recent = db.execute(
            "SELECT COUNT(*) FROM reservations WHERE reserved_at>=?", (cutoff,)
        ).fetchone()[0]
        print(f"  최근 {plan['window_seconds']}초 예약: {recent}/{plan['window_limit']}회")

    cooldowns = active_cooldowns(db, datetime.now())
    if cooldowns:
        for provider, model, rem, kind in cooldowns:
            print(f"  ⚠️ 쿨다운 중: [{provider}/{model}] ({kind}) 약 {rem:.0f}초 남음")
    else:
        print("  ✅ 최근 로그 기준 활성 쿨다운 없음 (모든 모델 사용 가능)")

    tasks = db.execute(
        """SELECT t.*, COUNT(r.id) AS used FROM tasks t
           LEFT JOIN reservations r ON r.plan_date=t.plan_date AND r.task_id=t.task_id
           WHERE t.plan_date=? GROUP BY t.plan_date,t.task_id ORDER BY t.rowid""",
        (plan_date,),
    ).fetchall()
    for task in tasks:
        print(f"  - {task['task_id']}: {task['task_name']} [{task['status']}] "
              f"{task['used']}/{task['request_budget']}회")


def request_gate(db: sqlite3.Connection, task_id: str, plan_date: str, note: str) -> int:
    ensure_today_plan(db, plan_date)
    now = datetime.now()
    db.execute("BEGIN IMMEDIATE")
    try:
        plan = db.execute("SELECT * FROM daily_plans WHERE plan_date=?", (plan_date,)).fetchone()
        if plan["paused"]:
            print(f"BLOCKED: 일시 중지 상태 ({plan['pause_reason']})")
            db.rollback()
            return 2

        cooldowns = active_cooldowns(db, now)
        if cooldowns:
            for provider, model, rem, kind in cooldowns:
                print(f"BLOCKED: [{provider}/{model}] ({kind}) 쿨다운 중 — 약 {rem:.0f}초 대기 (다른 모델 사용 권장)")
            db.rollback()
            return 2

        task = db.execute(
            "SELECT * FROM tasks WHERE plan_date=? AND task_id=?", (plan_date, task_id)
        ).fetchone()
        if not task:
            print(f"BLOCKED: 작업 ID '{task_id}' 없음 (기본 ID는 'work')")
            db.rollback()
            return 2
        if task["status"] in ("done", "blocked", "cancelled"):
            print(f"BLOCKED: 작업 상태가 {task['status']}입니다.")
            db.rollback()
            return 2

        total = db.execute(
            "SELECT COUNT(*) FROM reservations WHERE plan_date=?", (plan_date,)
        ).fetchone()[0]
        task_used = db.execute(
            "SELECT COUNT(*) FROM reservations WHERE plan_date=? AND task_id=?",
            (plan_date, task_id),
        ).fetchone()[0]
        allowed_daily = plan["daily_budget"] - plan["safety_reserve"]
        if total >= allowed_daily:
            print("BLOCKED: 오늘 일일 허용 예산을 모두 사용했습니다.")
            db.rollback()
            return 2
        if task_used >= task["request_budget"]:
            print("BLOCKED: 해당 작업 예산을 모두 사용했습니다.")
            db.rollback()
            return 2

        if plan["window_limit"]:
            cutoff = (now - timedelta(seconds=plan["window_seconds"])).isoformat(timespec="seconds")
            recent = db.execute(
                "SELECT COUNT(*) FROM reservations WHERE reserved_at>=?", (cutoff,)
            ).fetchone()[0]
            if recent >= plan["window_limit"]:
                print(f"BLOCKED: 최근 {plan['window_seconds']}초 예약 한도 도달 ({recent}/{plan['window_limit']})")
                db.rollback()
                return 2

        if plan["min_interval_seconds"] > 0:
            last = db.execute(
                "SELECT MAX(reserved_at) FROM reservations WHERE plan_date=?", (plan_date,)
            ).fetchone()[0]
            if last:
                elapsed = (now - datetime.fromisoformat(last)).total_seconds()
                wait = plan["min_interval_seconds"] - elapsed
                if wait > 0:
                    print(f"BLOCKED: 최소 간격 대기 필요 ({wait:.1f}초 후 가능)")
                    db.rollback()
                    return 2

        db.execute(
            "INSERT INTO reservations(plan_date,task_id,reserved_at,note) VALUES(?,?,?,?)",
            (plan_date, task_id, now.isoformat(timespec="seconds"), note),
        )
        db.commit()
        print(f"ALLOW: [{task_id}] {total + 1}/{allowed_daily}회 사용")
        return 0
    except Exception:
        db.rollback()
        raise


def set_pause(db: sqlite3.Connection, plan_date: str, paused: bool, reason: str) -> None:
    ensure_today_plan(db, plan_date)
    db.execute(
        "UPDATE daily_plans SET paused=?,pause_reason=? WHERE plan_date=?",
        (int(paused), reason if paused else "", plan_date),
    )
    db.commit()
    print("게이트 일시 중지" if paused else "게이트 재개")


def auto_check(log_path: Path, db: sqlite3.Connection, plan_date: str, log_tz: str) -> int:
    created = ensure_today_plan(db, plan_date)
    new_events = scan(log_path, db, log_tz)
    if created:
        print(f"[자동 초기화] {plan_date} 기본 계획 생성 (예산 {DEFAULT_BUDGET}, 예비 {DEFAULT_RESERVE})")
    if new_events:
        print(f"[로그 스캔] 새 rate-limit 이벤트 {new_events}건 감지")
    plan_status(db, plan_date)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--log-tz", choices=["auto", "local", "utc"], default="auto")
    sub = parser.add_subparsers(dest="command", required=True)

    auto = sub.add_parser("auto", help="Auto-create today's plan, scan logs, show status")
    auto.add_argument("--date", default=today())

    sub.add_parser("scan", help="Read new Hermes log lines (Nous, Qwen, all providers)")
    sub.add_parser("report", help="Summarize detected 429 events by provider/model")
    insp = sub.add_parser("inspect", help="Show raw matching log lines for debugging")
    insp.add_argument("--tail", type=int, default=15)

    watch = sub.add_parser("watch", help="Watch the log continuously")
    watch.add_argument("--interval", type=float, default=5)

    create = sub.add_parser("plan-create", help="Create or update a daily plan")
    create.add_argument("--budget", type=int, required=True)
    create.add_argument("--date", default=today())
    create.add_argument("--reserve", type=int, default=0)
    create.add_argument("--window-seconds", type=int, default=0)
    create.add_argument("--window-limit", type=int, default=0)
    create.add_argument("--min-interval", type=float, default=0)

    add = sub.add_parser("task-add", help="Add or update a task")
    add.add_argument("--id", required=True)
    add.add_argument("--name", required=True)
    add.add_argument("--budget", type=int, required=True)
    add.add_argument("--date", default=today())

    status = sub.add_parser("task-status", help="Change task status")
    status.add_argument("--id", required=True)
    status.add_argument("--status", choices=["todo", "in_progress", "done", "blocked", "cancelled"], required=True)
    status.add_argument("--date", default=today())

    plan = sub.add_parser("plan", help="Show plan usage")
    plan.add_argument("action", choices=["status"])
    plan.add_argument("--date", default=today())

    req = sub.add_parser("request", help="Check and reserve one prompt")
    req.add_argument("--task", default="work")
    req.add_argument("--date", default=today())
    req.add_argument("--note", default="")

    pause = sub.add_parser("pause", help="Pause local request gate")
    pause.add_argument("--reason", default="Manual pause")
    pause.add_argument("--date", default=today())

    resume = sub.add_parser("resume", help="Resume local request gate")
    resume.add_argument("--date", default=today())

    cancel = sub.add_parser("cancel", help="Cancel latest reservation for a task")
    cancel.add_argument("--task", default="work")
    cancel.add_argument("--date", default=today())
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    db = connect(args.db)
    try:
        if args.command == "auto":
            return auto_check(args.log, db, args.date, args.log_tz)
        elif args.command == "scan":
            print(f"새 이벤트 {scan(args.log, db, args.log_tz)}건 저장")
        elif args.command == "report":
            report(db)
        elif args.command == "inspect":
            inspect_log(args.log, args.tail)
        elif args.command == "watch":
            print(f"자동 감시 시작 (모든 프로바이더/Qwen/Nous): {args.log} (종료: Ctrl+C)")
            try:
                while True:
                    count = scan(args.log, db, args.log_tz)
                    if count:
                        print(f"\n[{datetime.now().isoformat(timespec='seconds')}] ⚠️ 리밋/재시도 {count}건 감지!")
                        for p, m, rem, kind in active_cooldowns(db, datetime.now()):
                            print(f"  -> [{p}/{m}] ({kind}) 약 {rem:.0f}초 대기 권장")
                    time.sleep(max(2.0, args.interval))
            except KeyboardInterrupt:
                print("\n감시 종료")
        elif args.command == "plan-create":
            plan_create(db, args.budget, args.date, args.reserve,
                        args.window_seconds, args.window_limit, args.min_interval)
        elif args.command == "task-add":
            task_add(db, args.date, args.id, args.name, args.budget)
        elif args.command == "task-status":
            task_status(db, args.date, args.id, args.status)
        elif args.command == "plan":
            plan_status(db, args.date)
        elif args.command == "request":
            scan(args.log, db, args.log_tz)
            return request_gate(db, args.task, args.date, args.note)
        elif args.command == "pause":
            set_pause(db, args.date, True, args.reason)
        elif args.command == "resume":
            set_pause(db, args.date, False, "")
        elif args.command == "cancel":
            cancel_reservation(db, args.task, args.date)
        return 0
    except (OSError, sqlite3.Error, ValueError) as exc:
        print(f"오류: {exc}", file=sys.stderr)
        return 1
    finally:
        db.close()


if __name__ == "__main__":
    raise SystemExit(main())