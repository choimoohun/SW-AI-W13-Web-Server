"""알림 서비스 (표준 라이브러리만 사용, 포트 8800).

- 백그라운드 스레드가 1초마다 MQ의 "reservation.paid" 토픽을 consume한다.
- 메시지 id로 중복을 걸러 알림을 NOTI_DATA_FILE(JSONL)에 남기고 ack한다.
- MQ가 죽거나 네트워크가 끊기면 무시하고 다음 주기에 재시도한다(메시지는 MQ에 남는다).
- GET /by-reservation?rid=, GET /recent, GET /
"""
import json
import os
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("NOTI_PORT", "8800"))
DATA_FILE = os.environ.get("NOTI_DATA_FILE", "data/notifications.jsonl")
MQ_URL = os.environ.get("MQ_URL", "http://127.0.0.1:8700").rstrip("/")
TOPIC = "reservation.paid"

_lock = threading.Lock()


def _load():
    rows = []
    try:
        with open(DATA_FILE, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rows.append(json.loads(line))
                except ValueError:
                    pass
    except FileNotFoundError:
        pass
    return rows


def _append(rec):
    directory = os.path.dirname(DATA_FILE)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(DATA_FILE, "a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def _post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(MQ_URL + path, data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=2) as resp:
        return resp.status, json.loads(resp.read() or b"{}")


def poll_loop():
    while True:
        try:
            _, out = _post("/consume", {"topic": TOPIC, "max": 10})
            ids = []
            for m in out.get("messages", []):
                mid = m.get("id")
                payload = m.get("payload") or {}
                rid = str(payload.get("reservation_id") or "")
                with _lock:
                    if any(r["mq_id"] == mid for r in _load()):
                        ids.append(mid)          # 이미 처리한 메시지면 그만 ack
                        continue
                    rec = {
                        "mq_id": mid,
                        "rid": rid,
                        "text": "결제가 완료되어 예약 %s 이(가) 확정되었습니다." % rid,
                        "time": int(time.time()),
                        "payment_id": payload.get("payment_id"),
                        "amount": payload.get("amount"),
                    }
                    _append(rec)
                    ids.append(mid)
            if ids:
                _post("/ack", {"ids": ids})
        except (urllib.error.URLError, OSError, ValueError):
            pass                            # MQ 장애는 무시, 다음 주기 재시도
        time.sleep(1)


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

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/":
            return self.send_json(200, {"service": "notifications", "ok": True})
        rows = _load()
        if u.path == "/by-reservation":
            rid = q.get("rid", "")
            found = [r for r in rows if r.get("rid") == rid]
            found.sort(key=lambda r: r.get("time", 0))
            return self.send_json(200, {"notifications": found})
        if u.path == "/recent":
            recent = sorted(rows, key=lambda r: r.get("time", 0), reverse=True)[:20]
            return self.send_json(200, {"notifications": recent})
        return self.send_json(404, {"error": "NOT_FOUND"})


if __name__ == "__main__":
    threading.Thread(target=poll_loop, daemon=True).start()
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
