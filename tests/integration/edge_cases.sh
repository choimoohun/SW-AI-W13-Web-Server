#!/bin/sh
# 잘못된 요청/큰 본문/백엔드 없음/클라이언트 끊김 시나리오 시연.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PORT=8088
BASE="http://127.0.0.1:$PORT"
LOG=/tmp/jg_edge_$$.log
REQ=/tmp/jg_edge_req_$$.txt
fail=0

pass() { echo "PASS  $1"; }
fail_case() { echo "FAIL  $1"; fail=1; }
expect_code() {
    want=$1; got=$2; name=$3
    [ "$got" = "$want" ] && pass "$name ($got)" || fail_case "$name (want $want, got $got)"
}

export FLIGHT_DATA_FILE=/dev/null
export WEB_PORT=$PORT
"$ROOT/webserver" -p "$PORT" -r "$ROOT/www" -c "$ROOT/conf/tcpserver.conf" >"$LOG" 2>&1 &
SRV=$!
trap 'kill -TERM "$SRV" 2>/dev/null || true; rm -f "$LOG" "$REQ"' EXIT

i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/health" && break
    i=$((i + 1)); sleep 0.2
done

echo '=== 잘못된 메서드 / 버전 ==='
code=$(curl -s -o /dev/null -w '%{http_code}' -X FOOBAR "$BASE/")
case "$code" in 405|501) pass " 지원하지 않는 메서드 ($code)";; *) fail_case "메서드 ($code)";; esac
resp=$(printf 'GET / HTTP/2.0\r\nHost: localhost\r\n\r\n' | nc -w 1 127.0.0.1 "$PORT" | head -1 | tr -d '\r' || true)
case "$resp" in *505*) pass "지원하지 않는 버전 ($resp)";; *) fail_case "버전 ($resp)";; esac

echo '=== 매우 긴 URI / 큰 본문 ==='
LONG_URI=$(python3 -c 'print("a" * 3000)')
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/$LONG_URI")
expect_code 414 "$code" '3000자 URI'
printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1048577\r\nConnection: close\r\n\r\nx' >"$REQ"
resp=$(nc -w 2 127.0.0.1 "$PORT" <"$REQ" | head -1 | tr -d '\r' || true)
case "$resp" in *413*) pass "1MB 초과 본문 ($resp)";; *) fail_case "큰 본문 ($resp)";; esac

# 본문 8192 바이트: 이전에는 수신 버퍼(8192)가 헤더+본문을 못 담아 431이 떴다.
BODY=/tmp/jg_edge_body_$$.txt
python3 -c 'print("x" * 8192, end="")' >"$BODY"
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST --data-binary "@$BODY" "$BASE/echo")
expect_code 200 "$code" '8KB 본문'
rm -f "$BODY"

echo '=== 백엔드 미기동 / 프록시 오류 ==='
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/orders/test")
expect_code 502 "$code" '백엔드 없음 502'

echo '=== 불완전 요청 후 클라이언트 종료 ==='
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n' | nc -w 1 127.0.0.1 "$PORT" >/dev/null 2>&1 || true
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health")
expect_code 200 "$code" '끊김 이후 서버 생존'

echo '=== 클라이언트 RST(EPIPE) 회수 ==='
python3 - "$PORT" <<'PY'
import socket, struct, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
             struct.pack("ii", 1, 0))   # 즉시 RST
s.sendall(b"POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\nabc")
s.close()                              # 응답을 읽지 않고 끊음 -> RST
PY
sleep 0.3
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$BASE/health")
expect_code 200 "$code" 'RST 이후 서버 생존'
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$BASE/")
expect_code 200 "$code" 'RST 이후 정상 요청'

echo '=== 유휴 타임아웃(5초) ==='
idle_out=$(python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port))
s.sendall(b"GET /health HTTP/1.1\r\nHost: x\r\n\r\n")
f = s.makefile("rb")
status = f.readline()
clen = 0
while True:
    line = f.readline()
    if line in (b"\r\n", b"\n", b""):
        break
    k, _, v = line.partition(b":")
    if k.strip().lower() == b"content-length":
        clen = int(v.strip())
if clen > 0:
    f.read(clen)
time.sleep(6)   # 유휴 상태로 놔둔다
s.settimeout(2)
try:
    data = s.recv(1)
except socket.timeout:
    data = b""
s.close()
print("closed" if data == b"" else "open")
PY
)
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$BASE/health")
expect_code 200 "$code" '유휴 정리 후 서버 생존'
if [ "$idle_out" = "closed" ]; then echo "PASS  유휴 5초 후 연결 종료"
else echo "FAIL  유휴 5초 후에도 열림 ($idle_out)"; fail=1; fi

echo '=== 접근 로그 샘플 ==='
grep '\[w' "$LOG" | head -3 || true

[ $fail -eq 0 ] && echo 'edge cases: all passed' || { echo 'edge cases: FAILED'; exit 1; }

echo '=== 요청 줄이 깨진 요청 → 400 ==='
resp=$(printf 'GET / HTTP\r\nHost: localhost\r\n\r\n' | nc -w 1 127.0.0.1 "$PORT" | head -1 | tr -d '\r' || true)
case "$resp" in *400*) pass "요청 줄 깨짐 ($resp)";; *) fail_case "요청 줄 깨짐 ($resp)";; esac

echo '=== 헤더를 끝까지 안 보내고 멈춘 연결 → 5초 뒤 서버 닫힘 ==='
idle_out=$(python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port), timeout=3)
s.sendall(b"GET /health HTTP/1.1\r\nHost: x\r\n")   # 마지막 \r\n\r\n 누락
time.sleep(6)   # 유휴 상태로 5초 뒤 닫힘
s.settimeout(2)
try:
    data = s.recv(1)
except socket.timeout:
    data = b""
s.close()
print("closed" if data == b"" else "open")
PY
)
if [ "$idle_out" = "closed" ]; then echo "PASS  헤더 미완 후 5초 뒤 연결 종료"
else echo "FAIL  헤더 미완 후 5초 후에도 열림 ($idle_out)"; fail=1; fi

echo '=== 헤더가 너무 큰 요청 → 431 ==='
{ printf 'GET / HTTP/1.1\r\nHost: localhost\r\nX-Big: '; head -c 9000 /dev/zero | tr '\0' 'a'; printf '\r\n\r\n'; } > "$REQ"
resp=$(nc -w 2 127.0.0.1 "$PORT" <"$REQ" | head -1 | tr -d '\r' || true)
case "$resp" in *431*) pass "헤더 과다 ($resp)";; *) fail_case "헤더 과다 ($resp)";; esac
rm -f "$REQ"

echo '=== 응답 상태 줄의 \r 제거 (화면에 )ASS 로 깨지는 문제) ==='
status_line=$(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc -w 1 127.0.0.1 "$PORT" | head -1 | tr -d '\r' || true)
case "$status_line" in
    "HTTP/1.1 200 OK") pass "상태줄 \r 제거 ($status_line)";;
    *) fail_case "상태줄 깨짐 ($status_line)";;
esac

[ $fail -eq 0 ] && echo 'edge cases: all passed' || { echo 'edge cases: FAILED'; exit 1; }
