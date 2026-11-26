#!/bin/sh
# 결제 서비스 연동 시연: 별도 프로세스 + 자기 파일 DB + 중복 방지 키 + 환불 + 독립 재시작.
set -u
# 이전에 띄운 서버가 포트를 잡고 있으면 테스트가 그 서버와 통신해 엉뚱한 결과가 나온다.
# (2026-11-24: 남아 있던 결제 서버 때문에 charge·종료 검사 FAIL) → 시작 전에 확인하고 멈춘다.
if command -v lsof >/dev/null 2>&1; then
    for p in 8500 8085; do
        pids=$(lsof -nP -tiTCP:"$p" -sTCP:LISTEN 2>/dev/null | tr '\n' ' ')
        if [ -n "$pids" ]; then
            echo "포트 $p 사용 중 (PID: $pids) — 이전 서버를 먼저 끄세요: kill $pids"
            exit 1
        fi
    done
fi
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/jg_payment_XXXXXX")
DATA="$TMP/payments.txt"
LOG="$TMP/log"
BASE=http://127.0.0.1:8085
fail=0

ok() { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

(
    ulimit -n 512
    echo "payments ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec env PAYMENT_DATA_FILE="$DATA" python3 tests/backends/serve_payments.py
) >"$LOG.pay" 2>&1 &
PAY=$!
grep 'ulimit' "$LOG.pay" || true

FLIGHT_DATA_FILE=/dev/null "$ROOT/webserver" -p 8085 -c "$ROOT/conf/backends.conf" >"$LOG.srv" 2>&1 &
SRV=$!
cleanup() {
    kill -TERM "$SRV" "$PAY" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT

wait_for() {
    url=$1
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "$url" && return 0
        i=$((i + 1)); sleep 0.2
    done
    return 1
}
wait_for http://127.0.0.1:8500/ || bad 'payments backend not ready'
wait_for "$BASE/health" || bad 'gateway not ready'

echo '--- charge ---'
curl -s -X POST -H 'Content-Type: application/json' -H 'Idempotency-Key: idem-1' \
    -d '{"reservation_id":"R-TEST-1","amount":89000,"method":"card","idempotency_key":"idem-1"}' \
    "$BASE/api/payments/charge" >"$LOG.charge"
python3 -c "import json;d=json.load(open('$LOG.charge'));assert d['status']=='CAPTURED';print(d['id'],d['amount'])" \
    && ok 'charge 201 / CAPTURED' || bad 'charge failed'
PID=$(python3 -c "import json;print(json.load(open('$LOG.charge'))['id'])")

echo '--- idempotency ---'
curl -s -X POST -H 'Content-Type: application/json' -H 'Idempotency-Key: idem-1' \
    -d '{"reservation_id":"R-TEST-1","amount":89000,"method":"card","idempotency_key":"idem-1"}' \
    "$BASE/api/payments/charge" >"$LOG.dup"
DUP=$(python3 -c "import json;print(json.load(open('$LOG.dup'))['id'])")
[ "$DUP" = "$PID" ] && ok 'same idempotency key -> same payment id' || bad "duplicate id=$DUP"

echo '--- lookup / refund / validation ---'
curl -s "$BASE/api/payments/by-reservation?rid=R-TEST-1" | grep -q '"amount": 89000' \
    && ok 'lookup by reservation' || bad 'lookup by reservation'
curl -s -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/payments/$PID/refund" >"$LOG.refund"
grep -q 'REFUNDED' "$LOG.refund" \
    && ok 'refund' || { bad 'refund'; cat "$LOG.refund"; echo; }
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
    -H 'Idempotency-Key: idem-bad' -d '{"reservation_id":"R-TEST-1","amount":0,"idempotency_key":"idem-bad"}' \
    "$BASE/api/payments/charge")
[ "$code" = "400" ] && ok 'invalid amount -> 400' || bad "invalid amount -> $code"

echo '--- independent lifecycle ---'
kill -9 "$PAY" 2>/dev/null || true
sleep 0.3
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/payments/$PID")
[ "$code" = "502" ] && ok 'payment process down -> 502' || bad "payment down -> $code"
curl -s -o /dev/null -w 'gateway /health -> %{http_code}\n' "$BASE/health"

(
    ulimit -n 512
    cd "$ROOT"
    exec env PAYMENT_DATA_FILE="$DATA" python3 tests/backends/serve_payments.py
) >>"$LOG.pay" 2>&1 &
PAY=$!
i=0; while [ $i -lt 50 ]; do curl -s -o /dev/null --max-time 1 http://127.0.0.1:8500/ && break; i=$((i+1)); sleep 0.2; done
curl -s "$BASE/api/payments/$PID" | grep -q 'REFUNDED' \
    && ok 'restart -> state restored from payment DB' || bad 'state not restored'

[ $fail -eq 0 ] && echo 'payment integration: all passed' || { echo 'payment integration: FAILED'; exit 1; }
