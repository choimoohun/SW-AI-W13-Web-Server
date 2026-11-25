#!/bin/sh
# 알림 서비스(8800)을 demo_up.sh와 같은 환경변수·데이터 파일로 백그라운드 재시작한다.
# 새 PID는 data/demo.pids에 추가한다(파일이 없으면 만든다).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PIDFILE="$ROOT/data/demo.pids"
LOG=/tmp/jg_demo_noti.log

wait_http() {
    i=0
    while [ $i -lt 100 ]; do
        curl -s -o /dev/null --max-time 1 "$1" && return 0
        i=$((i + 1)); sleep 0.1
    done
    return 1
}

if command -v lsof >/dev/null 2>&1; then
    info=$(lsof -nP -iTCP:8800 -sTCP:LISTEN 2>/dev/null | tail -n +2 || true)
    if [ -n "$info" ]; then
        echo "포트 8800 이미 사용 중:"
        echo "$info"
        echo "먼저 make noti-down 을 실행하세요."
        exit 1
    fi
fi

NOTI_PORT=8800 NOTI_DATA_FILE="$ROOT/data/notifications.jsonl" MQ_URL=http://127.0.0.1:8700 \
    python3 "$ROOT/tests/backends/serve_notifications.py" >"$LOG" 2>&1 &
newpid=$!
echo "$newpid" >>"$PIDFILE"
echo "알림 서비스 PID $newpid 를 $PIDFILE 에 추가했습니다."

wait_http http://127.0.0.1:8800/ || { echo "시작 실패: http://127.0.0.1:8800/"; exit 1; }
echo "알림 서비스 시작됨: http://127.0.0.1:8800/"
