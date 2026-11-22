"""mini-redis 저장소를 게이트웨이 경로로 감싼 얇은 어댑터.

mini-redis-repo의 Store를 그대로 import해서 쓴다. Flask가 없어도 되도록
표준 라이브러리 http.server만 사용한다. 게이트웨이가 prefix를 떼고 보내므로
여기서는 /get, /set, /delete를 받는다.
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

REPO = os.environ.get("MINI_REDIS_REPO", "/Users/velocitygoal/Desktop/jungle/mini-redis-repo")
sys.path.insert(0, REPO)
from mini_redis.store import Store  # noqa: E402

store = Store()


class H(BaseHTTPRequestHandler):
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
            return self.send_json(200, {"service": "mini-redis", "ok": True})
        if u.path == "/get":
            v = store.get(q.get("key", ""))
            return self.send_json(200, {"key": q.get("key", ""), "value": v}) if v is not None \
                else self.send_json(404, {"error": "not found"})
        return self.send_json(404, {"error": "no such endpoint"})

    def do_POST(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}")
        if u.path == "/set":
            store.set(body.get("key", ""), body.get("value"), body.get("ttl"))
            return self.send_json(200, {"ok": True})
        return self.send_json(404, {"error": "no such endpoint"})

    def do_DELETE(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/delete":
            return self.send_json(200, {"deleted": store.delete(q.get("key", ""))})
        return self.send_json(404, {"error": "no such endpoint"})


ThreadingHTTPServer(("127.0.0.1", 8000), H).serve_forever()
