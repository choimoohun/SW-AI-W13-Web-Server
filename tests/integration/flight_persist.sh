#!/bin/sh
# 영속 저장 검증: 예약 후 서버 재시작해도 예약/좌석/이력이 남는지 확인.
# 실행: make integration-persist
set -u
PORT=8080
BASE="http://127.0.0.1:$PORT"
DB="${TMPDIR:-/tmp}/jg_flight_persist_$$.txt"
export FLIGHT_DATA_FILE="$DB"
fail=0

contains() {
    case "$1" in *"$2"*) echo "PASS  $3";; *) echo "FAIL  $3"; fail=1;; esac
}

start() {
    ./webserver >/dev/null 2>&1 &
    SRV=$!
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "$BASE/" && return 0
        i=$((i + 1)); sleep 0.2
    done
    echo "FAIL  서버 기동 실패"; fail=1
    return 1
}

stop() {
    kill -TERM "$SRV" 2>/dev/null || true
    wait "$SRV" 2>/dev/null || true
    sleep 0.3
}

trap '[ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true; rm -f "$DB" "$DB.tmp"' EXIT

start
body=$(curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"name":"이철수","contact":"010-1111-2222","client":"persist-user"}' \
    "$BASE/api/flights/KE701/reserve")
contains "$body" '"id":"RS-0001"' "첫 실행에서 예약 생성"
body=$(curl -s "$BASE/api/flights/KE701")
contains "$body" '"seat_left":2' "예약 후 좌석 2석"
stop

start
body=$(curl -s "$BASE/api/reservations?client=persist-user")
contains "$body" '"RS-0001"' "재시작 후 예약 유지"
contains "$body" "이철수" "승객 정보 유지"
body=$(curl -s "$BASE/api/flights/KE701")
contains "$body" '"seat_left":2' "재시작 후 좌석 유지"
body=$(curl -s "$BASE/api/flights/KE701/timeline")
contains "$body" "예약 완료" "운항 이력 유지"
stop

if [ $fail -eq 0 ]; then echo 'flight persistence: all passed'
else echo 'flight persistence: FAILED'; fi
exit $fail
