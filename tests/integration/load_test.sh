#!/bin/sh
# 부하 테스트: 동시 접속 20 × 200요청. hey/wrk가 없으면 curl 병렬로 대체.
set -u
export FLIGHT_DATA_FILE=/dev/null
export WEB_PORT=8084
BASE="http://127.0.0.1:8084"
LOG=/tmp/jg_load_$$.log
fail=0

./webserver >"$LOG" 2>&1 &
SRV=$!
trap '[ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true' EXIT
i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/health" && break
    i=$((i + 1)); sleep 0.2
done

echo '=== 부하 테스트 (c=20, n=200) ==='
if command -v hey >/dev/null 2>&1; then
    hey -n 200 -c 20 "$BASE/health"
elif command -v wrk >/dev/null 2>&1; then
    wrk -c20 -t4 -d10s "$BASE/health"
else
    echo 'hey/wrk 없음 → curl 병렬 방식'
    START=$(date +%s%N)
    seq 1 200 | xargs -P 20 -I{} curl -s -o /dev/null -w '%{http_code}\n' "$BASE/health" > /tmp/jg_codes_$$.txt
    END=$(date +%s%N)
    MS=$(( (END - START) / 1000000 ))
    OK=$(grep -c '^200$' /tmp/jg_codes_$$.txt || true)
    BAD=$((200 - OK))
    echo "총 요청: 200 / 성공(200): $OK / Non-2xx: $BAD"
    echo "총 소요: ${MS}ms"
    if [ "$MS" -gt 0 ]; then
        echo "RPS(근사): $(( 200000 / MS ))"
    fi
    if [ "$BAD" -ne 0 ]; then fail=1; fi
    rm -f /tmp/jg_codes_$$.txt
fi

echo '=== 접근 로그 샘플 ==='
grep '\[w' "$LOG" | head -3 || true
rm -f "$LOG"
exit $fail
