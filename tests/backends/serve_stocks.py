"""mini-redis TTL 캐시를 앞에 둔 주식 데모 어댑터 (표준 라이브러리만 사용)."""
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

REPO = os.environ.get("MINI_REDIS_REPO", "/Users/velocitygoal/Desktop/jungle/mini-redis-repo")
sys.path.insert(0, REPO)
from mini_redis.store import Store  # noqa: E402

cache = Store()
DB = {1: {"id": 1, "name": "삼성", "price": 71000},
      2: {"id": 2, "name": "카카오", "price": 42000},
      3: {"id": 3, "name": "네이버", "price": 215000}}


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
        if u.path == "/":
            return self.send_json(200, {"service": "stocks", "ok": True})
        if u.path == "/stocks/top":
            started = time.perf_counter()
            hit = cache.get("top_stocks")
            if hit is None:
                time.sleep(0.2)             # 느린 DB 흉내
                hit = sorted(DB.values(), key=lambda s: -s["price"])[:3]
                cache.set("top_stocks", hit, ttl=60)
                source = "db"
            else:
                source = "cache"
            return self.send_json(200, {"service": "stocks", "source": source,
                                        "elapsed_ms": round((time.perf_counter() - started) * 1000, 1),
                                        "stocks": hit})
        return self.send_json(404, {"error": "no such endpoint"})


ThreadingHTTPServer(("127.0.0.1", 8002), H).serve_forever()
