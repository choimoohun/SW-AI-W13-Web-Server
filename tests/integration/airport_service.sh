#!/bin/sh
# 공항 정보 서비스(별도 프로세스 2대)를 게이트웨이 뒤에 두고 확인한다.
# 1) 경로 라우팅 + rewrite: /api/airports/ICN -> 백엔드 /ICN
# 2) Round-Robin: 응답한 서버가 A, B로 번갈아 나온다
# 3) 장애 격리: B를 죽이면 공항 경로만 일부 502, 항공편 API와 /health는 정상
# 4) 독립 재시작: B를 다시 띄우면 게이트웨이 재시작 없이 전부 200
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=/tmp/jg_airports_$$
BASE=http://127.0.0.1:8085
fail=0

pass_or_fail() {  # $1=조건(0이면 통과) $2=설명
    if [ "$1" -eq 0 ]; then echo "PASS  $2"; else echo "FAIL  $2"; fail=1; fi
}

start_airport() {  # $1=port $2=name
    python3 "$ROOT/tests/backends/serve_airports.py" --port "$1" --name "$2" >"$LOG.$2" 2>&1 &
    echo $!
}

wait_http() {  # $1=url
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "$1" && return 0
        i=$((i + 1)); sleep 0.1
    done
    return 1
}

A=$(start_airport 8601 A)
B=$(start_airport 8602 B)
FLIGHT_DATA_FILE=/dev/null "$ROOT/webserver" -p 8085 -c "$ROOT/conf/backends.conf" >"$LOG.srv" 2>&1 &
SRV=$!

cleanup() {
    kill -TERM "$SRV" "$A" "$B" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -f "$LOG".*
}
trap cleanup EXIT

wait_http "$BASE/health" || { echo "FAIL  게이트웨이가 뜨지 않음"; exit 1; }
wait_http "http://127.0.0.1:8601/" || { echo "FAIL  공항 서비스 A가 뜨지 않음"; exit 1; }
wait_http "http://127.0.0.1:8602/" || { echo "FAIL  공항 서비스 B가 뜨지 않음"; exit 1; }

echo '=== 1) 경로 라우팅 + rewrite ==='
body=$(curl -s "$BASE/api/airports/ICN")
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/airports/ICN")
echo "$body" | grep -q '인천국제공항'
pass_or_fail $? "/api/airports/ICN -> 200 인천국제공항 (code=$code)"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/airports/XXX")
[ "$code" = "404" ]
pass_or_fail $? "없는 공항 코드 -> 백엔드의 404가 그대로 전달 (code=$code)"

echo '=== 2) Round-Robin ==='
servers=""
for i in 1 2 3 4; do
    s=$(curl -s "$BASE/api/airports/NRT" | sed -n 's/.*"server": *"\([AB]\)".*/\1/p')
    servers="$servers$s"
done
echo "응답 서버 순서: $servers"
case "$servers" in *A*) hasA=0 ;; *) hasA=1 ;; esac
case "$servers" in *B*) hasB=0 ;; *) hasB=1 ;; esac
[ $hasA -eq 0 ] && [ $hasB -eq 0 ]
pass_or_fail $? "4번 요청에 A와 B가 모두 응답"
case "$servers" in ABAB|BABA) rr=0 ;; *) rr=1 ;; esac
pass_or_fail $rr "A, B가 번갈아 응답 (Round-Robin)"

echo '=== 3) 장애 격리: B 종료 ==='
kill -9 "$B" 2>/dev/null || true
wait "$B" 2>/dev/null || true
sleep 0.3
codes=""
for i in 1 2 3 4; do
    codes="$codes $(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/airports/ICN")"
done
echo "공항 경로 응답 코드:$codes"
echo "$codes" | grep -q 200 && echo "$codes" | grep -q 502
pass_or_fail $? "살아 있는 A는 200, 죽은 B 차례만 502"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/flights")
[ "$code" = "200" ]
pass_or_fail $? "공항 서비스 장애 중에도 /api/flights -> 200"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health")
[ "$code" = "200" ]
pass_or_fail $? "공항 서비스 장애 중에도 /health -> 200"

echo '=== 4) 독립 재시작: B만 다시 띄움 ==='
B=$(start_airport 8602 B)
wait_http "http://127.0.0.1:8602/" || true
codes=""
for i in 1 2 3 4; do
    codes="$codes $(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/airports/ICN")"
done
echo "공항 경로 응답 코드:$codes"
echo "$codes" | grep -q 502
[ $? -ne 0 ]
pass_or_fail $? "게이트웨이 재시작 없이 B 복구 후 전부 200"

if [ $fail -eq 0 ]; then echo "airport-service: all passed"; else echo "airport-service: FAILED"; exit 1; fi
