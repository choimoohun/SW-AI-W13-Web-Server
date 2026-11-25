#!/bin/sh
# SO_REUSEADDR(tcpserver.c) 검증.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PORT=${RESTART_PORT:-8091}
BASE="http://127.0.0.1:$PORT"
fail=0

run_once() { # $1=log  $2=레이블. 성공 0, 실패 1.
    log=$1; label=$2
    "$ROOT/webserver" -p "$PORT" >"$log" 2>&1 &
    srv=$!
    i=0
    ready=0
    while [ $i -lt 50 ]; do
        if curl -s -o /dev/null --max-time 1 "$BASE/health"; then ready=1; break; fi
        i=$((i + 1)); sleep 0.2
    done
    if [ "$ready" = "1" ]; then
        echo "PASS  $label"
        kill -TERM "$srv" 2>/dev/null || true
        wait "$srv" 2>/dev/null || true
        return 0
    fi
    echo "FAIL  $label"
    kill -TERM "$srv" 2>/dev/null || true
    wait "$srv" 2>/dev/null || true
    return 1
}

LOG1=/tmp/jg_restart1_$$.log
LOG2=/tmp/jg_restart2_$$.log

run_once "$LOG1" "첫 번째 시작" || fail=1
sleep 0.3
run_once "$LOG2" "두 번째 시작(같은 포트)" || fail=1

if grep -q "Address already in use" "$LOG1" "$LOG2" 2>/dev/null; then
    echo "FAIL  Address already in use 발생"; fail=1
else
    echo "PASS  Address already in use 없음"
fi

rm -f "$LOG1" "$LOG2"
[ $fail -eq 0 ] && echo 'restart test: all passed' || { echo 'restart test: FAILED'; exit 1; }
