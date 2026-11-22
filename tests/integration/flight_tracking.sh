#!/bin/sh
# 출발/도착 자동 추적 통합 검증 (가상 시계 배속 3000).
# 실행: make integration-tracking
set -u
export FLIGHT_DATA_FILE=/dev/null
export FLIGHT_TIME_MODE=sim
export FLIGHT_SIM_SPEED=3000
export WEB_PORT=8081
PORT=8081
BASE="http://127.0.0.1:$PORT"
fail=0

contains() {
    case "$1" in *"$2"*) echo "PASS  $3";; *) echo "FAIL  $3 (못 찾음: $2)"; fail=1;; esac
}

start() {
    ./webserver >/dev/null 2>&1 &
    SRV=$!
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "$BASE/api/ops/clock" && return 0
        i=$((i + 1)); sleep 0.2
    done
    echo "FAIL  서버 기동 실패"; fail=1
    return 1
}

wait_status() {
    want=$1
    i=0
    while [ $i -lt 60 ]; do
        body=$(curl -s "$BASE/api/flights/KE701")
        case "$body" in *"\"status\":\"$want\""*) return 0;; esac
        i=$((i + 1)); sleep 0.2
    done
    echo "FAIL  KE701이 $want 상태가 되지 않음"; fail=1
    return 1
}

trap '[ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true' EXIT

start
body=$(curl -s "$BASE/api/ops/clock")
contains "$body" '"mode":"sim"' "가상 시계 모드"
contains "$body" '"speed":3000' "배속 설정"

echo '--- 지연 입력 ---'
body=$(curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"delay_min":30}' "$BASE/api/ops/flights/KE202/delay")
contains "$body" '"status":"DELAYED"' "KE202 지연 상태 반영"
contains "$body" '"delay_applied_min":30' "지연 분 표시"
body=$(curl -s "$BASE/api/flights/KE202/timeline")
contains "$body" "지연 30분" "지연 이벤트 기록"

echo '--- 자동 출발/도착 추적 ---'
wait_status DEPARTED
body=$(curl -s "$BASE/api/flights/KE701")
if printf '%s' "$body" | grep -Eq '"actual_depart_epoch":[1-9]'; then
    echo "PASS  실제 출발 시각 기록"
else
    echo "FAIL  실제 출발 시각 기록"; fail=1
fi
wait_status ARRIVED
body=$(curl -s "$BASE/api/flights/KE701")
if printf '%s' "$body" | grep -Eq '"actual_arrive_epoch":[1-9]'; then
    echo "PASS  실제 도착 시각 기록"
else
    echo "FAIL  실제 도착 시각 기록"; fail=1
fi
body=$(curl -s "$BASE/api/flights/KE701/timeline")
contains "$body" "자동 출발 기록" "출발 이벤트"
contains "$body" "자동 도착 기록" "도착 이벤트"

kill -TERM "$SRV" 2>/dev/null || true
wait "$SRV" 2>/dev/null || true
if [ $fail -eq 0 ]; then echo 'flight tracking: all passed'
else echo 'flight tracking: FAILED'; fi
exit $fail
