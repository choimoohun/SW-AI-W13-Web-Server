#!/bin/sh
# data/demo.pids에 저장된 데모 프로세스를 종료한다. 포트가 남아 있으면 누구인지 알려준다.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PIDFILE="$ROOT/data/demo.pids"

if [ ! -f "$PIDFILE" ]; then
    echo "PID 파일이 없습니다: $PIDFILE (make demo-up 먼저)"
    exit 1
fi

for pid in $(cat "$PIDFILE"); do
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        echo "종료 요청: $pid"
    else
        echo "이미 없음: $pid"
    fi
done
for pid in $(cat "$PIDFILE"); do
    i=0
    while kill -0 "$pid" 2>/dev/null && [ $i -lt 50 ]; do
        i=$((i + 1)); sleep 0.1
    done
done
rm -f "$PIDFILE"

left=0
for p in 8085 8500 8601 8602 8700 8800; do
    if command -v lsof >/dev/null 2>&1; then
        info=$(lsof -nP -iTCP:"$p" -sTCP:LISTEN 2>/dev/null | tail -n +2 || true)
        if [ -n "$info" ]; then
            echo "포트 $p 여전히 사용 중:"
            echo "$info"
            left=1
        fi
    fi
done
[ $left -eq 0 ] && echo "데모 종료 완료." || exit 1
