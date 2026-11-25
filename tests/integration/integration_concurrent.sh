#!/bin/sh
# 동시 다중 클라이언트: 동시 curl 50개로 /flights.html과 /api/flights를 요청해
# 전부 200인지 확인하는 통합 테스트.
#
# 실행: demo-down 상태에서 서버를 자체 띄우고 검증 (포트 충돌 방지).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1

BIN=./webserver
PORT=${WEB_PORT:-8080}
N=50
LOG=$(mktemp)
fail=0

# 테스트는 demo-down 상태(포트 비움)에서 실행한다.
"$BIN" -p "$PORT" -c "$ROOT/conf/backends.conf" >"$LOG" 2>&1 &
SRV=$!
trap 'kill -TERM "$SRV" 2>/dev/null || true; rm -f "$LOG"' EXIT

i=0
while [ $i -lt 50 ]; do
    if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$PORT/health"; then break; fi
    i=$((i + 1)); sleep 0.2
done

code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

echo "=== 동시 다중 클라이언트 ($N 개, 포트 $PORT) ==="

run_set() { # $1=경로
    local path=$1
    local codes
    codes=$(seq "$N" | xargs -P 50 -I{} curl -s -o /dev/null -w '%{http_code}\n' \
        "http://127.0.0.1:$PORT$path" 2>/dev/null)
    local ok=0 bad=0
    for c in $codes; do
        [ "$c" = "200" ] && ok=$((ok + 1)) || bad=$((bad + 1))
    done
    echo "경로 $path : 200 = $ok, 비200 = $bad (요청 $N)"
    if [ "$bad" -ne 0 ]; then
        echo "FAIL  $path (비200 발생)" >&2
        fail=1
    else
        echo "PASS  $path (동시 $N 전부 200)"
    fi
}

run_set /flights.html
run_set /api/flights

echo ""
if [ $fail -eq 0 ]; then echo "integration-concurrent: all passed"; else echo "integration-concurrent: FAILED"; exit 1; fi
