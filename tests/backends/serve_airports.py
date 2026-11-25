"""별도 프로세스로 동작하는 공항 정보 서비스 (표준 라이브러리만 사용).

- 항공편 상세 화면이 출발/도착 공항 정보를 보여 줄 때 쓴다.
- 같은 서비스를 포트만 바꿔 두 대 띄우고(--name A, --name B),
  게이트웨이가 Round-Robin으로 나눠 보낸다.
- 게이트웨이는 /api/airports/ prefix를 / 로 바꿔서 보낸다.
  예: /api/airports/ICN -> 이 서버의 /ICN

실행: python3 tests/backends/serve_airports.py --port 8601 --name A
"""
import argparse
import json
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

AIRPORTS = {
    "ICN": ("인천국제공항", "인천", "T1 · T2"),
    "NRT": ("나리타국제공항", "도쿄", "T1 · T2 · T3"),
    "JFK": ("존 F. 케네디 국제공항", "뉴욕", "T1 · T4 · T5 · T7 · T8"),
    "LAX": ("로스앤젤레스 국제공항", "로스앤젤레스", "T1~T8 · TBIT"),
    "SIN": ("싱가포르 창이공항", "싱가포르", "T1 · T2 · T3 · T4"),
    "HKG": ("홍콩국제공항", "홍콩", "T1"),
    "BKK": ("수완나품 국제공항", "방콕", "메인 터미널"),
    "CDG": ("샤를 드골 공항", "파리", "T1 · T2 · T3"),
    "LHR": ("히드로 공항", "런던", "T2 · T3 · T4 · T5"),
    "SYD": ("시드니 공항", "시드니", "T1 · T2 · T3"),
    "DXB": ("두바이 국제공항", "두바이", "T1 · T2 · T3"),
}
LEVELS = ("원활", "보통", "혼잡")


def congestion(code):
    """시연용 혼잡도: 공항 코드와 현재 시각(시)으로 정해지는 값. 같은 시간대엔 같은 값."""
    hour = int(time.time() // 3600)
    return LEVELS[(zlib.crc32(code.encode()) + hour) % len(LEVELS)]


class Handler(BaseHTTPRequestHandler):
    server_name_label = "A"

    def log_message(self, format, *args):
        pass

    def send_json(self, code, body):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Served-By", "airport-" + self.server_name_label)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0].strip("/")
        if path == "":
            self.send_json(200, {"airports": sorted(AIRPORTS), "server": self.server_name_label})
            return
        code = path.upper()
        if code not in AIRPORTS:
            self.send_json(404, {"error": "not_found", "code": code,
                                 "server": self.server_name_label})
            return
        name, city, terminal = AIRPORTS[code]
        self.send_json(200, {
            "code": code, "name": name, "city": city, "terminal": terminal,
            "congestion": congestion(code), "server": self.server_name_label,
        })


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8601)
    ap.add_argument("--name", default="A")
    args = ap.parse_args()
    Handler.server_name_label = args.name
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
