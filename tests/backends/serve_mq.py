"""메시지 큐 시뮬레이션 (표준 라이브러리만 사용, 포트 8700).

- POST /publish {topic, payload} -> 201 {id}
- POST /consume {topic, max}    -> ready 메시지를 꺼내 inflight로. lease 10초.
                                  lease가 만료되면 다시 ready로 풀린다(재소비).
- POST /ack {ids}               -> done 처리
- GET  /stats?topic=            -> {ready, inflight, done}
- GET  /                        -> 헬스

데이터는 MQ_DATA_FILE(JSONL)에 보관한다. 재시작하면 inflight를 ready로 되돌린다.
실제 AMQP/Kafka가 아니라, '큐에 넣고 lease로 재시도한다'는 흐름만 재현한다.
"""
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("MQ_PORT", "8700"))
DATA_FILE = os.environ.get("MQ_DATA_FILE", "data/mq.jsonl")
LEASE_SEC = 10

_lock = threading.Lock()
_seq = [0]


def _load():
    msgs = []
    try:
        with open(DATA_FILE, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    m = json.loads(line)
                except ValueError:
                    continue
                # 재시작 시 미완료 inflight는 다시 ready로.
                if m.get("state") == "inflight":
                    m["state"] = "ready"
                    m["lease_until"] = 0
                msgs.append(m)
    except FileNotFoundError:
        pass
    return msgs


def _save(msgs):
    directory = os.path.dirname(DATA_FILE)
    if directory:
        os.makedirs(directory, exist_ok=True)
    tmp = DATA_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for m in msgs:
            f.write(json.dumps(m, ensure_ascii=False) + "\n")
    os.replace(tmp, DATA_FILE)


def _find(msgs, mid):
    return next((m for m in msgs if m.get("id") == mid), None)


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
            return self.send_json(200, {"service": "mq", "ok": True})
        if u.path == "/stats":
            topic = q.get("topic", "")
            with _lock:
                msgs = _load()
            sel = [m for m in msgs if not topic or m.get("topic") == topic]
            now = int(time.time())
            return self.send_json(200, {
                "ready": sum(1 for m in sel
                             if m["state"] == "ready"
                             or (m["state"] == "inflight" and m.get("lease_until", 0) <= now)),
                "inflight": sum(1 for m in sel if m["state"] == "inflight"),
                "done": sum(1 for m in sel if m["state"] == "done"),
            })
        return self.send_json(404, {"error": "NOT_FOUND"})

    def do_POST(self):
        u = urlparse(self.path)
        body = self.body_json() or {}
        if u.path == "/publish":
            topic = str(body.get("topic") or "")
            payload = body.get("payload")
            if not topic or payload is None:
                return self.send_json(400, {"error": "BAD_REQUEST",
                                            "message": "topic, payload required"})
            with _lock:
                msgs = _load()
                _seq[0] += 1
                mid = "msg-%d-%d" % (int(time.time() * 1000), _seq[0])
                msgs.append({"id": mid, "topic": topic, "payload": payload,
                             "state": "ready", "lease_until": 0,
                             "created": int(time.time())})
                _save(msgs)
            return self.send_json(201, {"id": mid})

        if u.path == "/consume":
            topic = str(body.get("topic") or "")
            try:
                limit = max(1, min(int(body.get("max", 1)), 100))
            except (TypeError, ValueError):
                limit = 1
            with _lock:
                msgs = _load()
                now = int(time.time())
                picked = []
                for m in msgs:
                    if len(picked) >= limit:
                        break
                    if m.get("topic") != topic:
                        continue
                    if m["state"] == "ready" or (
                            m["state"] == "inflight" and m.get("lease_until", 0) <= now):
                        m["state"] = "inflight"
                        m["lease_until"] = now + LEASE_SEC
                        picked.append({"id": m["id"], "topic": m["topic"],
                                       "payload": m["payload"]})
                _save(msgs)
            return self.send_json(200, {"messages": picked})

        if u.path == "/ack":
            ids = body.get("ids") or []
            if not isinstance(ids, list):
                return self.send_json(400, {"error": "BAD_REQUEST", "message": "ids[] required"})
            with _lock:
                msgs = _load()
                for mid in ids:
                    m = _find(msgs, str(mid))
                    if m:
                        m["state"] = "done"
                _save(msgs)
            return self.send_json(200, {"acked": len(ids)})

        return self.send_json(404, {"error": "NOT_FOUND"})


if __name__ == "__main__":
    with _lock:
        msgs = _load()
        _save(msgs)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
