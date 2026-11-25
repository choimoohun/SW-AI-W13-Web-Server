#!/bin/sh
# C-05/FR-03/FR-05/FR-07/NFR-04/미니CDN 통합 검증. 포트 8083 사용.
set -u
export FLIGHT_DATA_FILE=/dev/null
PORT=8083
BASE="http://127.0.0.1:$PORT"
LOG=/tmp/jg_requirements_$$.log
fail=0

contains() {
    case "$1" in *"$2"*) echo "PASS  $3";; *) echo "FAIL  $3 (못 찾음: $2)"; fail=1;; esac
}

cleanup() {
    [ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true
    [ -n "${B1:-}" ] && kill -TERM "$B1" 2>/dev/null || true
    [ -n "${B2:-}" ] && kill -TERM "$B2" 2>/dev/null || true
}
trap cleanup EXIT

python3 tests/integration/fixtures/backend.py 8401 >/dev/null 2>&1 &
B1=$!
python3 tests/integration/fixtures/backend.py 8402 >/dev/null 2>&1 &
B2=$!
sleep 0.5

./webserver -p "$PORT" -r ./www -c conf/tcpserver.conf >"$LOG" 2>&1 &
SRV=$!
i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/health" && break
    i=$((i + 1)); sleep 0.2
done

echo '--- C-05 CLI/설정 + FR-03 헤더 ---'
hdr=$(curl -s -D - -o /dev/null "$BASE/")
contains "$hdr" "Date:" "Date 헤더"
contains "$hdr" "Server: jungle-c/1.0" "Server 헤더"
contains "$hdr" "X-Cache: MISS" "첫 요청 CDN MISS"
contains "$hdr" "Cache-Control: public, max-age=" "Cache-Control 헤더"
contains "$hdr" "ETag: W/" "ETag 헤더"
hdr=$(curl -s -D - -o /dev/null "$BASE/")
contains "$hdr" "X-Cache: HIT" "두 번째 요청 CDN HIT"

echo '--- FR-05 내장 동적 엔드포인트 ---'
body=$(curl -s "$BASE/health")
contains "$body" '"status":"ok"' "/health"
body=$(curl -s -X POST -d 'hello world' "$BASE/echo")
contains "$body" '"echo":"hello world"' "/echo"

echo '--- FR-07 리버스 프록시 + Round-Robin ---'
seq 1 4 | while read -r _; do curl -s "$BASE/api/orders/test"; echo; done > /tmp/jg_rr_$$.txt
cat /tmp/jg_rr_$$.txt
rr=$(grep -o '"server": [0-9]*' /tmp/jg_rr_$$.txt | tr '\n' ' ')
contains "$rr" "8401" "백엔드 8401 사용"
contains "$rr" "8402" "백엔드 8402 사용"
# 분산 순서: 연속 요청은 반드시 다른 백엔드로 가야 한다(Round-Robin).
seq_ok=1
prev=""
while read -r v; do
    if [ -n "$prev" ] && [ "$v" = "$prev" ]; then seq_ok=0; fi
    prev="$v"
done <<EOF
$(grep -o '"server": [0-9]*' /tmp/jg_rr_$$.txt | awk '{print $2}')
EOF
if [ $seq_ok -eq 1 ]; then echo "PASS  Round-Robin 순서(연속 요청 교대)"
else echo "FAIL  Round-Robin 순서 (같은 백엔드 연속)"; fail=1; fi

# rewrite: /api/rw/test -> 백엔드는 /v1/test 를 봄.
body=$(curl -s "$BASE/api/rw/test")
contains "$body" '"path": "/v1/test"' "rewrite prefix 교체"
body=$(curl -s "$BASE/api/orders/test")
contains "$body" '"xff": "127.0.0.1"' "X-Forwarded-For 값"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/cache/miss")
if [ "$code" = "502" ]; then echo "PASS  백엔드 없음 -> 502"
else echo "FAIL  백엔드 없음 -> $code"; fail=1; fi

echo '--- 프록시가 워커를 블로킹하지 않는지 ---'
curl -s -o /dev/null -w '%{time_total}' "$BASE/api/orders/slow" > /tmp/jg_slow_$$.txt &
SLOW_PID=$!
sleep 0.3
FAST=$(curl -s -o /dev/null -w '%{time_total}' "$BASE/health")
wait $SLOW_PID
SLOW=$(cat /tmp/jg_slow_$$.txt)
FAST_MS=$(awk -v t="$FAST" 'BEGIN { print int(t * 1000) }')
SLOW_MS=$(awk -v t="$SLOW" 'BEGIN { print int(t * 1000) }')
echo "  slow=${SLOW_MS}ms / health=${FAST_MS}ms"
if [ "$FAST_MS" -lt 1000 ] && [ "$SLOW_MS" -ge 1500 ]; then
    echo "PASS  느린 백엔드가 워커를 막지 않음"
else
    echo "FAIL  느린 백엔드가 워커를 블로킹함"; fail=1
fi
rm -f /tmp/jg_slow_$$.txt

echo '--- 백엔드 응답 지연 -> 504 ---'
code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 12 "$BASE/api/orders/hang")
if [ "$code" = "504" ]; then echo "PASS  백엔드 지연 -> 504"
else echo "FAIL  백엔드 지연 -> $code"; fail=1; fi

echo '--- NFR-04 접근 로그 ---'
sleep 0.3
if grep -Eq '\[w[0-9]+\]' "$LOG"; then echo "PASS  워커 ID 포함"
else echo "FAIL  워커 ID 포함 (못 찾음: [wN])"; fail=1; fi
contains "$(cat "$LOG")" "127.0.0.1" "클라이언트 IP 포함"
contains "$(cat "$LOG")" "ms" "지연시간 포함"

rm -f /tmp/jg_rr_$$.txt "$LOG"
if [ $fail -eq 0 ]; then echo 'requirements integration: all passed'
else echo 'requirements integration: FAILED'; fi
exit $fail
