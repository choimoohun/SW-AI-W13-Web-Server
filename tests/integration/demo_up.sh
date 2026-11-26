#!/bin/sh
# 데모 전체를 백그라운드로 띄운다: C 서버(8085) + 결제(8500) + 공항 A/B(8601/8602)
# + 메시지 큐(8700) + 알림(8800). PID는 data/demo.pids에 저장한다.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PIDFILE="$ROOT/data/demo.pids"
LOGDIR=/tmp
fail=0

port_info() { # $1=port
    if command -v lsof >/dev/null 2>&1; then
        lsof -nP -iTCP:"$1" -sTCP:LISTEN 2>/dev/null | tail -n +2 || true
    fi
}

for p in 8085 8500 8601 8602 8700 8800; do
    info=$(port_info "$p")
    if [ -n "$info" ]; then
        echo "포트 $p 이미 사용 중:"
        echo "$info"
        fail=1
    fi
done
[ $fail -eq 0 ] || { echo "데모를 띄우기 전에 위 프로세스를 끄세요. (make demo-down)"; exit 1; }

wait_http() {
    i=0
    while [ $i -lt 100 ]; do
        curl -s -o /dev/null --max-time 1 "$1" && return 0
        i=$((i + 1)); sleep 0.1
    done
    return 1
}

: >"$PIDFILE"

MQ_PORT=8700 MQ_DATA_FILE="$ROOT/data/mq.jsonl" \
    python3 "$ROOT/tests/backends/serve_mq.py" >"$LOGDIR/jg_demo_mq.log" 2>&1 &
echo $! >>"$PIDFILE"

NOTI_PORT=8800 NOTI_DATA_FILE="$ROOT/data/notifications.jsonl" MQ_URL=http://127.0.0.1:8700 \
    python3 "$ROOT/tests/backends/serve_notifications.py" >"$LOGDIR/jg_demo_noti.log" 2>&1 &
echo $! >>"$PIDFILE"

PAYMENT_PORT=8500 PAYMENT_DATA_FILE="$ROOT/data/payments.txt" \
    PAYMENT_OUTBOX_FILE="$ROOT/data/payments.txt.outbox" MQ_URL=http://127.0.0.1:8700 \
    python3 "$ROOT/tests/backends/serve_payments.py" >"$LOGDIR/jg_demo_pay.log" 2>&1 &
echo $! >>"$PIDFILE"

python3 "$ROOT/tests/backends/serve_airports.py" --port 8601 --name A >"$LOGDIR/jg_demo_airA.log" 2>&1 &
echo $! >>"$PIDFILE"
python3 "$ROOT/tests/backends/serve_airports.py" --port 8602 --name B >"$LOGDIR/jg_demo_airB.log" 2>&1 &
echo $! >>"$PIDFILE"

"$ROOT/webserver" -c "$ROOT/conf/backends.conf" >"$LOGDIR/jg_demo_server.log" 2>&1 &
echo $! >>"$PIDFILE"

for url in http://127.0.0.1:8700/ http://127.0.0.1:8800/ http://127.0.0.1:8500/ \
           http://127.0.0.1:8085/health; do
    wait_http "$url" || { echo "시작 실패: $url"; exit 1; }
done

echo "데모 시작됨. PID 파일: $PIDFILE"
echo "  C 서버     http://127.0.0.1:8085/flights.html"
echo "  결제       :8500   공항 A :8601 / B :8602"
echo "  메시지 큐  :8700   알림   :8800"
echo "종료: make demo-down"
