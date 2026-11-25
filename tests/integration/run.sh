#!/bin/sh
# 정적 파일 서빙 기능(통합) 테스트 — 마일스톤 1~4.
# 서버를 띄우고 curl 로 검증한 뒤 종료한다. 요구사항 §7: 정적 200/403/404, POST, 바이너리.
# 실행: sh tests/integration/run.sh   (프로젝트 루트에서, ./webserver 빌드 후)
set -u
PORT=8080
BASE="http://127.0.0.1:$PORT"

./webserver &
SRV=$!
SHUTDOWN_RC=0
cleanup() {
    kill -TERM "$SRV" 2>/dev/null || true
    wait "$SRV" 2>/dev/null || SHUTDOWN_RC=$?
}
trap cleanup EXIT
# 서버 준비 대기(ASAN 빌드는 시작이 느릴 수 있음) — 최대 ~10초 폴링
ready=0
i=0
while [ $i -lt 50 ]; do
    if curl -s -o /dev/null --max-time 1 "$BASE/"; then ready=1; break; fi
    i=$((i+1)); sleep 0.2
done
if [ "$ready" != 1 ]; then
    echo "FAIL  서버 시작 실패 (port $PORT)"
    exit 1
fi

fail=0
code() { curl -s -o /dev/null -w "%{http_code}" "$@"; }
expect() { # expect <expected> <desc> <curl args...>
    exp="$1"; desc="$2"; shift 2
    got=$(code "$@")
    if [ "$got" = "$exp" ]; then echo "PASS  $desc -> $got"; else echo "FAIL  $desc -> $got (expected $exp)"; fail=1; fi
}

expect 200 "GET /"            "$BASE/"
expect 200 "GET /index.html"  "$BASE/index.html"
expect 200 "GET /style.css"   "$BASE/style.css"
expect 200 "GET /sample.txt"  "$BASE/sample.txt"
expect 404 "GET /nope"        "$BASE/nope"
expect 403 "GET /../etc/passwd"      --path-as-is "$BASE/../etc/passwd"
expect 403 "GET /%2e%2e/etc/passwd"  --path-as-is "$BASE/%2e%2e/etc/passwd"
expect 405 "POST / (body)"    -X POST -d x "$BASE/"

# 바이너리 무결성: 받은 PNG 가 원본과 바이트 일치
if curl -s "$BASE/test.png" | cmp -s - www/test.png; then
    echo "PASS  binary integrity (test.png)"
else
    echo "FAIL  binary integrity (test.png)"; fail=1
fi

# MIME 확인
ct=$(curl -s -D - -o /dev/null "$BASE/test.png" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print $2}')
if [ "$ct" = "image/png" ]; then echo "PASS  MIME image/png"; else echo "FAIL  MIME -> $ct"; fail=1; fi

# keep-alive 파이프라인: 한 번에 보낸 두 요청의 응답이 순서대로, 본문까지 온전해야 한다.
# PR #22 리뷰 R11의 회귀 테스트. 16KB 이하 파일은 CDN 버퍼로 나가므로,
# 파일 스트리밍(file_fd) 경로를 타도록 큰 파일을 임시로 만들어 쓴다.
BIG=www/pipeline_big.txt
python3 -c "open('$BIG','w').write('pipeline-' * 5000)"
pipe_check() { # pipe_check <첫 경로> <둘째 경로>  → 두 응답 본문이 원본 파일과 같으면 ok
python3 - "$PORT" "$1" "$2" <<'PY'
import socket, sys
port, a, b = int(sys.argv[1]), sys.argv[2], sys.argv[3]
s = socket.create_connection(("127.0.0.1", port), timeout=3)
s.sendall(("GET %s HTTP/1.1\r\nHost: t\r\n\r\n" % a).encode()
          + ("GET %s HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n" % b).encode())
d = b""
try:
    while True:
        x = s.recv(65536)
        if not x:
            break
        d += x
except OSError:
    pass
def take(buf):  # 응답 하나를 Content-Length로 잘라 (상태줄, 본문, 나머지)
    head, _, rest = buf.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    n = 0
    for l in lines[1:]:
        k, _, v = l.partition(b":")
        if k.strip().lower() == b"content-length":
            n = int(v)
    return lines[0], rest[:n], rest[n:]
def want(path):
    return open("www" + path, "rb").read() if path.startswith("/") and path != "/health" else None
s1, b1, rest = take(d)
s2, b2, rest = take(rest)
ok = s1.startswith(b"HTTP/1.1 200") and s2.startswith(b"HTTP/1.1 200") and rest == b""
for path, body in ((a, b1), (b, b2)):
    w = want(path)
    ok = ok and (body.startswith(b"{") if w is None else body == w)
print("ok" if ok else "bad")
PY
}
r=$(pipe_check /pipeline_big.txt /style.css)
if [ "$r" = "ok" ]; then echo "PASS  pipelined keep-alive: big file + file"; else echo "FAIL  pipelined keep-alive: big file + file -> $r"; fail=1; fi
r=$(pipe_check /health /pipeline_big.txt)
if [ "$r" = "ok" ]; then echo "PASS  pipelined keep-alive: json + big file"; else echo "FAIL  pipelined keep-alive: json + big file -> $r"; fail=1; fi
rm -f "$BIG"

trap - EXIT   # 아래에서 직접 정리하므로 EXIT 트랩은 끈다.
cleanup
if [ "$SHUTDOWN_RC" = "0" ]; then echo "PASS  SIGTERM 종료 코드 0"
else echo "FAIL  SIGTERM 종료 코드 $SHUTDOWN_RC"; fail=1; fi

if [ "$fail" = 0 ]; then echo "integration: all passed"; else echo "integration: FAILED"; fi
exit $fail
