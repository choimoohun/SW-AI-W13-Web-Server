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
resp=$(printf 'GET / HTTP/2.0\r\nHost: localhost\r\n\r\n' | nc -w 1 127.0.0.1 "$PORT" | head -1 || true)
case "$resp" in *505*) pass "지원하지 않는 버전 ($resp)";; *) fail_case "버전 ($resp)";; esac

echo '=== 매우 긴 URI / 큰 본문 ==='
LONG_URI=$(python3 -c 'print("a" * 3000)')
code=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/$LONG_URI")
expect_code 414 "$code" '3000자 URI'
printf 'POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1048577\r\nConnection: close\r\n\r\nx' >"$REQ"
resp=$(nc -w 2 127.0.0.1 "$PORT" <"$REQ" | head -1 || true)
case "$resp" in *413*) pass "1MB 초과 본문 ($resp)";; *) fail_case "큰 본문 ($resp)";; esac

echo '=== 백엔드 미기동 / 프록시 오류 ==='
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/orders/test")
expect_code 502 "$code" '백엔드 없음 502'

echo '=== 불완전 요청 후 클라이언트 종료 ==='
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n' | nc -w 1 127.0.0.1 "$PORT" >/dev/null 2>&1 || true
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health")
expect_code 200 "$code" '끊김 이후 서버 생존'

echo '=== 접근 로그 샘플 ==='
grep '\[w' "$LOG" | head -3 || true

[ $fail -eq 0 ] && echo 'edge cases: all passed' || { echo 'edge cases: FAILED'; exit 1; }
