"""테스트용 백엔드. 포트와 X-Forwarded-For를 JSON으로 되돌려 준다."""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if "/hang" in self.path:
            time.sleep(8)
        if "/slow" in self.path:
            time.sleep(2)
        body = json.dumps({
            "server": PORT,
            "path": self.path,
            "xff": self.headers.get("X-Forwarded-For", ""),
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
