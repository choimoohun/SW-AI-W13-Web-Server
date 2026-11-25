#!/bin/sh
# 데모에서 알림 서비스(8800)만 끈다. data/demo.pids의 PID는 그대로 둔다.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PIDFILE="$ROOT/data/demo.pids"

# data/demo.pids에 없어도 포트 8800을 물고 있는 serve_notifications.py를 끈다.
find_pid() {
    if command -v lsof >/dev/null 2>&1; then
        pids=$(lsof -nP -tiTCP:8800 -sTCP:LISTEN 2>/dev/null | tr '\n' ' ')
        for cand in $pids; do
            if ps -o command= -p "$cand" | grep -q 'serve_notifications.py'; then
                echo "$cand"
                return 0
            fi
        done
    fi
    [ -f "$PIDFILE" ] || return 1
    for cand in $(cat "$PIDFILE"); do
        if kill -0 "$cand" 2>/dev/null &&
           ps -o command= -p "$cand" | grep -q 'serve_notifications.py'; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

pid=$(find_pid) || {
    echo "실행 중인 알림 서비스를 못 찾았습니다."
    echo "포트 8800 확인: lsof -nP -iTCP:8800 -sTCP:LISTEN"
    exit 1
}

kill -TERM "$pid" 2>/dev/null || true
echo "종료 요청: $pid"

i=0
while kill -0 "$pid" 2>/dev/null && [ $i -lt 50 ]; do
    i=$((i + 1)); sleep 0.1
done

if kill -0 "$pid" 2>/dev/null; then
    kill -KILL "$pid" 2>/dev/null || true
    echo "강제 종료: $pid"
else
    echo "알림 서비스 종료됨 (PID $pid)."
fi

if command -v lsof >/dev/null 2>&1; then
    info=$(lsof -nP -iTCP:8800 -sTCP:LISTEN 2>/dev/null | tail -n +2 || true)
    if [ -n "$info" ]; then
        echo "포트 8800 여전히 사용 중:"
        echo "$info"
        exit 1
    fi
fi
echo "알림 서비스가 꺼졌습니다. 다시 띄우기: make noti-up"
