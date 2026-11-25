#!/bin/sh
# 결제 → 메시지 큐 → 알림 서비스 비동기 연동 검증.
#
# ① 결제 201 → 5초 안에 알림 생성
# ② 알림 서비스 죽음 → 결제는 201, MQ에 메시지 남음, 알림 조회만 502, 항공편 API는 200
# ③ 알림 서비스 재시작 → 밀린 알림 처리, ready = 0
# ④ MQ 죽음 → 결제 201(아웃박스 보관) → MQ 복구 후 알림 전달
# ⑤ 같은 idempotency_key 재결제 → 200, 알림 1건 유지
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BASE=http://127.0.0.1:8085
LOG=/tmp/jg_noti_$$
fail=0

pass() { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; fail=1; }

# 포트 선점 확인(옛 데모 서버가 테스트를 가로채지 않게).
if command -v lsof >/dev/null 2>&1; then
    for p in 8085 8500 8700 8800; do
        pids=$(lsof -nP -tiTCP:"$p" -sTCP:LISTEN 2>/dev/null | tr '\n' ' ')
        if [ -n "$pids" ]; then
            echo "포트 $p 사용 중 (PID: $pids) — 먼저 끄세요: kill $pids"
            exit 1
        fi
    done
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/jg_noti_XXXXXX")
PAY_FILE="$TMP/payments.txt"
OUTBOX="$TMP/payments.txt.outbox"
MQ_FILE="$TMP/mq.jsonl"
NOTI_FILE="$TMP/notifications.jsonl"

MQ_PID=""; NOTI_PID=""; PAY_PID=""; SRV=""
cleanup() {
    for pid in $SRV $PAY_PID $NOTI_PID $MQ_PID; do
        [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT

wait_http() { # $1=url $2=최대초
    i=0; max=$(( ${2:-5} * 10 ))
    while [ $i -lt "$max" ]; do
        curl -s -o /dev/null --max-time 1 "$1" && return 0
        i=$((i + 1)); sleep 0.1
    done
    return 1
}

wait_for() { # $1=설명 $2=최대초; 성공 조건은 호출자가 파이프를 통해 넘긴 명령.
    desc=$1; secs=${2:-10}; i=0
    while [ $i -lt $((secs * 10)) ]; do
        if eval "$3"; then pass "$desc"; return 0; fi
        i=$((i + 1)); sleep 0.1
    done
    bad "$desc"
    return 1
}

start_mq() {
    MQ_PORT=8700 MQ_DATA_FILE="$MQ_FILE" python3 "$ROOT/tests/backends/serve_mq.py" >"$LOG.mq" 2>&1 &
    MQ_PID=$!
    wait_http http://127.0.0.1:8700/ || { echo "FAIL  MQ 시작 실패"; exit 1; }
}

start_noti() {
    NOTI_PORT=8800 NOTI_DATA_FILE="$NOTI_FILE" MQ_URL=http://127.0.0.1:8700 \
        python3 "$ROOT/tests/backends/serve_notifications.py" >"$LOG.noti" 2>&1 &
    NOTI_PID=$!
    wait_http http://127.0.0.1:8800/ || { echo "FAIL  알림 서비스 시작 실패"; exit 1; }
}

charge() { # $1=idempotency_key $2=rid $3=amount
    curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
        -d "{\"reservation_id\":\"$2\",\"amount\":$3,\"method\":\"card\",\"idempotency_key\":\"$1\"}" \
        "$BASE/api/payments/charge"
}

notice_count() { # $1=rid
    curl -s "$BASE/api/notifications/by-reservation?rid=$1" |
        sed -n 's/.*"notifications":[[:space:]]*\[\([^]]*\)\].*/\1/p' | grep -o '"mq_id"' | wc -l | tr -d ' '
}

noti_text() { # $1=rid
    curl -s "$BASE/api/notifications/by-reservation?rid=$1" |
        sed -n 's/.*"text":[[:space:]]*"\([^"]*\)".*/\1/p' | head -1
}

mq_ready() {
    curl -s "$BASE/api/mq/stats?topic=reservation.paid" | sed -n 's/.*"ready":[[:space:]]*\([0-9]*\).*/\1/p'
}

start_mq
start_noti
PAYMENT_PORT=8500 PAYMENT_DATA_FILE="$PAY_FILE" PAYMENT_OUTBOX_FILE="$OUTBOX" \
    MQ_URL=http://127.0.0.1:8700 \
    python3 "$ROOT/tests/backends/serve_payments.py" >"$LOG.pay" 2>&1 &
PAY_PID=$!
wait_http http://127.0.0.1:8500/ || { echo "FAIL  결제 서비스 시작 실패"; exit 1; }
FLIGHT_DATA_FILE=/dev/null "$ROOT/webserver" -p 8085 -c "$ROOT/conf/backends.conf" >"$LOG.srv" 2>&1 &
SRV=$!
wait_http "$BASE/health" || { echo "FAIL  게이트웨이 시작 실패"; exit 1; }

echo '=== ① 결제 201 → 알림 생성 ==='
code=$(charge key-A rsv-A 1000)
[ "$code" = "201" ] && pass "결제 201 (code=$code)" || bad "결제 응답 $code"
wait_for "5초 안에 알림 생성" 6 "[ \"\$(notice_count rsv-A)\" -ge 1 ]"
txt=$(noti_text rsv-A)
case "$txt" in *확정되었습니다*) pass "알림 문구: $txt";; *) bad "알림 문구: $txt";; esac

echo '=== ② 알림 서비스 죽음 → 결제는 정상 ==='
kill -9 "$NOTI_PID" 2>/dev/null || true
wait "$NOTI_PID" 2>/dev/null || true
NOTI_PID=""; sleep 0.3
# 알림 서비스가 죽은 뒤 결제해야 메시지가 MQ에 남는다(소비되지 않음).
code=$(charge key-B rsv-B 2000)
[ "$code" = "201" ] && pass "알림 서비스 다운 중 결제 201 (code=$code)" || bad "결제 응답 $code"
wait_for "MQ에 메시지 남음 (ready >= 1)" 5 "[ \"\$(mq_ready)\" -ge 1 ]"
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$BASE/api/notifications/by-reservation?rid=rsv-B")
[ "$code" = "502" ] && pass "알림 조회만 502 (code=$code)" || bad "알림 조회 code=$code"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/flights")
[ "$code" = "200" ] && pass "항공편 API는 200" || bad "항공편 API code=$code"

echo '=== ③ 알림 서비스 재시작 → 밀린 알림 처리 ==='
start_noti
# lease 최대 10초를 고려해 넉넉히 20초 대기.
wait_for "밀린 알림(rsv-B) 처리" 20 "[ \"\$(notice_count rsv-B)\" -ge 1 ]"
ready=$(mq_ready)
[ "${ready:-1}" = "0" ] && pass "MQ ready = 0" || bad "MQ ready = $ready"

echo '=== ④ MQ 죽음 → 아웃박스 보관 → 복구 후 전달 ==='
kill -9 "$MQ_PID" 2>/dev/null || true
wait "$MQ_PID" 2>/dev/null || true
MQ_PID=""; sleep 0.3
code=$(charge key-C rsv-C 3000)
[ "$code" = "201" ] && pass "MQ 다운 중 결제 201 (code=$code)" || bad "결제 응답 $code"
ob=$(wc -l <"$OUTBOX" 2>/dev/null | tr -d ' ')
[ "${ob:-0}" -ge 1 ] && pass "아웃박스에 이벤트 보관 (${ob}줄)" || bad "아웃박스가 비었음"
start_mq
wait_for "MQ 복구 후 알림 전달" 15 "[ \"\$(notice_count rsv-C)\" -ge 1 ]"

echo '=== ⑤ 같은 idempotency_key 재결제 → 200, 알림 1건 ==='
code=$(charge key-A rsv-A 1000)
[ "$code" = "200" ] && pass "멱등 재결제 200 (code=$code)" || bad "멱등 재결제 code=$code"
sleep 2
n=$(notice_count rsv-A)
[ "$n" = "1" ] && pass "알림 1건 유지 (count=$n)" || bad "알림 수 $n"

[ $fail -eq 0 ] && echo 'notification-service: all passed' || { echo 'notification-service: FAILED'; exit 1; }
