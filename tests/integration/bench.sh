#!/bin/sh
# 부하 테스트 (ab 기반): docs/bench.md 에 표 저장.
#
# 대상: 정적 /flights.html, 프록시 경로 /api/airports/ICN
# 정적: 동시 1, 10, 50, 100, 100 (각 3000건)
# 프록시: ab -n 1000 -s 10, 동시 1, 10, 50
#
# 실행: 공항 백엔드(8601/8602)가 살아 있는 단독 웹서버(포트 8180)를 띄우고
#      /api/airports/ICN 이 200 인지 확인한 뒤에만 측정한다.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1

BIN=./webserver
PORT=${BENCH_PORT:-8180}
OUT="$ROOT/docs/bench.md"
LOG=$(mktemp)

: > "$OUT"

{
    echo "# 부하 테스트 (ApacheBench)"
    echo ""
    echo '- 대상: /flights.html(정적), /api/airports/ICN(리버스 프록시)'
    echo '- 정적: 동시 1, 10, 50, 100, 100 / 건당 3000건'
    echo '- 프록시: ab -n 1000 -s 10, 동시 1, 10, 50'
    echo "- 방법: \`ab -n N -c c <url>\`"
    echo ""
} >> "$OUT"

# 단독 웹서버: 백엔드가 살아 있는 상태로 프록시 경로 검증.
"$BIN" -p "$PORT" -c "$ROOT/conf/backends.conf" >"$LOG" 2>&1 &
SRV=$!
trap 'kill -TERM "$SRV" 2>/dev/null || true; rm -f "$LOG"' EXIT

# 서버 준비 대기
i=0
while [ $i -lt 50 ]; do
    if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$PORT/health"; then break; fi
    i=$((i + 1)); sleep 0.2
done

# 공항 서버(가이드웨이) 200 확인 — 이 확인이 끝나기 전엔 측정하지 않는다.
if ! curl -s -o /dev/null --max-time 3 "http://127.0.0.1:$PORT/api/airports/ICN"; then
    echo "FAIL: /api/airports/ICN 이 200 이 아님 (공항 백엔드 확인 필요)" >&2
    cat "$LOG" >&2
    exit 1
fi
echo "OK: /api/airports/ICN = 200" | tee -a "$OUT"

{
    echo "| 대상 | 동시(c) | 요청 수 | 처리(수) | 실패(수) | Non-2xx(수) | 평균 응답(ms) | RPS |"
    echo "|---|---|---|---|---|---|---|---|"
} >> "$OUT"

run_ab() { # $1=url $2=concurrency $3=timeout(sec, 0=ab 기본값)
    local url=$1 c=$2 to=$3
    local path ok fail non2xx tpr rps
    path=${url#http://127.0.0.1:$PORT}
    local out
    if [ "$to" -gt 0 ] 2>/dev/null; then
        out=$(ab -n "$N" -c "$c" "$url" -s "$to" 2>&1)
    else
        out=$(ab -n "$N" -c "$c" "$url" 2>&1)
    fi
    ok=$(printf '%s\n' "$out" | awk '/^Complete requests:/ {print $3; exit}')
    fail=$(printf '%s\n' "$out" | awk '/^Failed requests:/ {print $3; exit}')
    non2xx=$(printf '%s\n' "$out" | awk '/^Non-2xx responses:/ {print $3; exit}')
    [ -n "$non2xx" ] || non2xx=0
    tpr=$(printf '%s\n' "$out" | awk '/^Time per request:/ {print $4; exit}')
    rps=$(printf '%s\n' "$out" | awk '/^Requests per second:/ {print $4; exit}')
    printf '| %s | %s | %s | %s | %s | %s | %s | %s |\n' \
        "$path" "$c" "$N" "$ok" "$fail" "$non2xx" "$tpr" "$rps" >> "$OUT"
}

# 정적 /flights.html: 3000건, 동시 1, 10, 50, 100, 100
N=3000
for c in 1 10 50 100 100; do
    run_ab "http://127.0.0.1:$PORT/flights.html" "$c" 0
done

# 프록시 /api/airports/ICN: 1000건, -s 10, 동시 1, 10, 50
N=1000
for c in 1 10 50; do
    run_ab "http://127.0.0.1:$PORT/api/airports/ICN" "$c" 10
done

{
    echo ""
    echo "## 참고"
    echo ""
    echo "- ab가 없는 환경에서는 `make bench`가 실패한다(ab: $(command -v ab || echo 없음))."
    echo "- 정적 파일 `/flights.html` 은 서버 내부 처리, `/api/airports/ICN` 은 리버스 프록시 경로다."
    echo "- 공항 백엔드(8601/8602)가 살아 있어야 /api/airports/ICN 이 200 으로 응답한다."
} >> "$OUT"

echo "docs/bench.md 작성 완료 ($(grep -c '^|' "$OUT") 행)"
cat "$OUT"
