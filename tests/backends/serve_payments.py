"""별도 프로세스로 동작하는 결제 서비스 (표준 라이브러리만 사용).

- 자기 파일 DB(PAYMENT_DATA_FILE)를 가진다.
- Idempotency-Key 또는 body의 idempotency_key로 중복 충전을 막는다.
- 게이트웨이는 /api/payments/ prefix를 / 로 바꿔서 이 서버로 보낸다.
"""
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

DATA_FILE = os.environ.get("PAYMENT_DATA_FILE", "data/payments.txt")
_lock = threading.Lock()


def _load():
    rows = {}
    try:
        with open(DATA_FILE, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("|")
                if len(parts) != 8:
                    continue
                pid, rid, amount, method, status, key, created, updated = parts
                rows[pid] = {
                    "id": pid, "reservation_id": rid, "amount": int(amount),
                    "method": method, "status": status, "idempotency_key": key,
                    "created": int(created), "updated": int(updated),
                }
    except FileNotFoundError:
        pass
    return rows


def _save(rows):
    directory = os.path.dirname(DATA_FILE)
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp = DATA_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for p in rows.values():
            f.write("|".join([
                p["id"], p["reservation_id"], str(p["amount"]), p["method"],
                p["status"], p["idempotency_key"], str(p["created"]), str(p["updated"]),
            ]) + "\n")
    os.replace(tmp, DATA_FILE)


def _by_reservation(rows, rid):
    found = [p for p in rows.values() if p["reservation_id"] == rid]
    return sorted(found, key=lambda p: p["created"], reverse=True)[0] if found else None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def send_json(self, code, body):
        data = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 64 * 1024:
            return None
        try:
            return json.loads(self.rfile.read(n) or b"{}")
        except (ValueError, TypeError):
            return None

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/":
            return self.send_json(200, {"service": "payments", "ok": True})
        rows = _load()
        if u.path == "/by-reservation":
            p = _by_reservation(rows, q.get("rid", ""))
            return self.send_json(200, p) if p else self.send_json(404, {"error": "NOT_FOUND"})
        pid = u.path.strip("/")
        p = rows.get(pid)
        return self.send_json(200, p) if p else self.send_json(404, {"error": "NOT_FOUND"})

    def do_POST(self):
        u = urlparse(self.path)
        body = self.body_json()
        if body is None:
            return self.send_json(400, {"error": "BAD_REQUEST", "message": "invalid json"})
        key = self.headers.get("Idempotency-Key") or body.get("idempotency_key") or ""
        if u.path == "/charge":
            rid = str(body.get("reservation_id") or "")
            try:
                amount = int(body.get("amount"))
            except (TypeError, ValueError):
                return self.send_json(400, {"error": "BAD_REQUEST", "message": "amount required"})
            method = str(body.get("method") or "card")
            if not rid or amount <= 0 or not key:
                return self.send_json(400, {"error": "BAD_REQUEST",
                                            "message": "reservation_id, amount>0, idempotency_key required"})
            with _lock:
                rows = _load()
                existing = next((p for p in rows.values()
                                 if p["idempotency_key"] == key), None)
                if existing:
                    return self.send_json(200, existing)
                now = int(time.time())
                pid = "pay-" + str(now) + "-" + key[-8:]
                p = {"id": pid, "reservation_id": rid, "amount": amount,
                     "method": method, "status": "CAPTURED",
                     "idempotency_key": key, "created": now, "updated": now}
                rows[pid] = p
                time.sleep(0.15)          # 외부 PG 왕복 흉내
                _save(rows)
            return self.send_json(201, p)
        if u.path.endswith("/refund"):
            pid = u.path.rsplit("/", 2)[-2]
            with _lock:
                rows = _load()
                p = rows.get(pid)
                if not p:
                    return self.send_json(404, {"error": "NOT_FOUND"})
                if p["status"] != "REFUNDED":
                    p["status"] = "REFUNDED"
                    p["updated"] = int(time.time())
                    _save(rows)
            return self.send_json(200, p)
        return self.send_json(404, {"error": "NOT_FOUND"})


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8500), Handler).serve_forever()
