#!/bin/sh
# 별도 프로세스/리소스 제한/독립 재시작 시연.
# mini-redis 코드와 mini-dbms 데모 코드를 실제로 백엔드로 띄우고 C 게이트웨이에 연결한다.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JUNGLE=$(dirname "$ROOT")
DATA=/tmp/jg_backends_data_$$
PKG=/tmp/jg_dbms_pkg_$$
LOG=/tmp/jg_backends_$$.log
mkdir -p "$DATA" "$PKG"
ln -s "$JUNGLE/mini-dbms/docs/study/WHY_DBMS/why_dbms_demo" "$PKG/dbms_demo"
export PYTHONPATH="$PKG"

echo "=== 자원 제한을 다르게 준 별도 프로세스 ==="
(
    ulimit -n 64
    echo "mini-redis  ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec python3 tests/backends/serve_mini_redis.py
) >"$LOG.redis" 2>&1 &
REDIS=$!
(
    ulimit -n 128
    echo "stocks      ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec python3 tests/backends/serve_stocks.py
) >"$LOG.stocks" 2>&1 &
STOCKS=$!
(
    ulimit -n 256
    echo "dbms-8401   ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec python3 -m dbms_demo.app --mode file --role 주문 --port 8401 --data "$DATA"
) >"$LOG.db1" 2>&1 &
DB1=$!
(
    ulimit -n 256
    echo "dbms-8402   ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec python3 -m dbms_demo.app --mode file --role 주문2 --port 8402 --data "$DATA"
) >"$LOG.db2" 2>&1 &
DB2=$!
(
    ulimit -n 512
    echo "payments     ulimit -n=$(ulimit -n)"
    cd "$ROOT"
    exec env PAYMENT_DATA_FILE="$DATA/payments.txt" python3 tests/backends/serve_payments.py
) >"$LOG.pay" 2>&1 &
PAY=$!

cat "$LOG".redis "$LOG".stocks "$LOG".db1 "$LOG".db2 "$LOG".pay | grep 'ulimit' || true

python3 -c "from dbms_demo.store import make_store; make_store('file', '$DATA').seed(50)" || true

FLIGHT_DATA_FILE=/dev/null "$ROOT/webserver" -p 8085 -c "$ROOT/conf/backends.conf" >"$LOG.srv" 2>&1 &
SRV=$!
BASE=http://127.0.0.1:8085

cleanup() {
    kill -TERM "$SRV" 2>/dev/null || true
    kill -TERM "$REDIS" "$STOCKS" "$DB1" "$DB2" "$PAY" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$DATA" "$PKG" "$LOG"*
}
trap cleanup EXIT

i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/health" && break
    i=$((i + 1)); sleep 0.2
done
for port in 8000 8002 8401 8402 8500; do
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$port/" && break
        i=$((i + 1)); sleep 0.2
    done
done

echo '=== mini-redis 백엔드 (/api/cache/) ==='
curl -s -o /dev/null -w 'get before set -> %{http_code}\n' "$BASE/api/cache/get?key=k"
curl -s -o /dev/null -w 'set            -> %{http_code}\n' -X POST -d '{"key":"k","value":"v"}' "$BASE/api/cache/set"
curl -s "$BASE/api/cache/get?key=k"; echo

echo '=== stocks 백엔드 (/api/stocks/) ==='
curl -s "$BASE/api/stocks/stocks/top" | head -c 300; echo
curl -s "$BASE/api/stocks/stocks/top" | head -c 300; echo

echo '=== mini-dbms 백엔드 2대 Round-Robin (/api/orders/) ==='
for i in 1 2 3 4; do
    curl -s "$BASE/api/orders/orders" | head -c 120; echo
done

echo '=== 결제 백엔드 (/api/payments/) ==='
curl -s "$BASE/api/payments/pay-test" | head -c 120; echo

echo '=== 독립 라이프사이클: 8401만 죽였다가 다시 띄움 ==='
kill -9 "$DB1" 2>/dev/null || true
sleep 0.3
for i in 1 2 3 4; do
    curl -s -o /dev/null -w '%{http_code} ' "$BASE/api/orders/orders"
done
echo
(
    ulimit -n 256
    cd "$ROOT"
    exec python3 -m dbms_demo.app --mode file --role 주문 --port 8401 --data "$DATA"
) >"$LOG.db1.new" 2>&1 &
DB1=$!
sleep 1
for i in 1 2 3 4; do
    curl -s -o /dev/null -w '%{http_code} ' "$BASE/api/orders/orders"
done
echo
echo '=== 워커는 그대로 살아 있음 ==='
curl -s -o /dev/null -w '/health -> %{http_code}\n' "$BASE/health"
