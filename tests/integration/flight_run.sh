#!/bin/sh
# 항공편 데모 통합 검증 (STEP6).
# 실행: make integration-flight  (프로젝트 루트에서, ./webserver 빌드 후)
#
# 1) 기본 API 상태코드/본문
# 2) before(무락) 100동시 예약 → 초과 예약 확인
# 3) after(뮤텍스) 100동시 예약 → 201 1건 + 409 99건 확인
# 4) 출발/도착 추적(상태 변경 + 타임라인)
set -u
export FLIGHT_DATA_FILE=/dev/null   # 테스트는 영속 저장 없이 시드 상태에서 시작
PORT=8080
BASE="http://127.0.0.1:$PORT"
fail=0

code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
expect() {
    exp=$1; desc=$2; shift 2
    got=$(code "$@")
    if [ "$got" = "$exp" ]; then echo "PASS  $desc -> $got"
    else echo "FAIL  $desc -> $got (expected $exp)"; fail=1; fi
}

start_server() {
    ./webserver &
    SRV=$!
    i=0
    while [ $i -lt 50 ]; do
        curl -s -o /dev/null --max-time 1 "$BASE/" && return 0
        i=$((i + 1)); sleep 0.2
    done
    echo "FAIL  서버 기동 실패"; fail=1
    return 1
}

stop_server() {
    kill -TERM "$SRV" 2>/dev/null || true
    wait "$SRV" 2>/dev/null || true
    sleep 0.3
}

restart_server() {
    stop_server
    start_server
}

# 응답 본문에 문자열이 포함됐는지 확인
contains() {
    body=$1; needle=$2; desc=$3
    case "$body" in
        *"$needle"*) echo "PASS  $desc";;
        *) echo "FAIL  $desc (못 찾음: $needle)"; fail=1;;
    esac
}

trap '[ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true' EXIT

start_server

echo '--- 기본 API ---'
expect 200 "GET /api/flights" "$BASE/api/flights"
expect 200 "GET /api/flights/KE701" "$BASE/api/flights/KE701"
expect 404 "GET /api/flights/NOPE" "$BASE/api/flights/NOPE"
expect 400 "POST 예약 이름 누락" -X POST -d '{}' "$BASE/api/flights/KE701/reserve"
expect 405 "PUT /api/flights" -X PUT "$BASE/api/flights"
expect 404 "GET /api/nope" "$BASE/api/nope"

body=$(curl -s "$BASE/api/flights")
contains "$body" '"seat_left":1' "KE999 시드 좌석 1석 확인"
contains "$body" '"KE202"' "KE202 시드 확인"

echo '--- 예약 / 내 예약 / 취소 ---'
body=$(curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"name":"김영희","contact":"010-5555-1234","client":"tester"}' \
    "$BASE/api/flights/KE701/reserve")
contains "$body" '"id":"RS-0001"' "예약 생성"
contains "$body" '"contact":"010-5555-1234"' "연락처 저장"
body=$(curl -s "$BASE/api/reservations?client=tester")
contains "$body" '"RS-0001"' "내 예약 조회"
expect 400 "다른 client 예약 취소 거부" -X POST -d '{"client":"other"}' \
    "$BASE/api/reservations/RS-0001/cancel"
expect 200 "본인 예약 취소" -X POST -d '{"client":"tester"}' \
    "$BASE/api/reservations/RS-0001/cancel"
body=$(curl -s "$BASE/api/flights/KE701")
contains "$body" '"seat_left":3' "취소 후 좌석 복원"

echo '--- keep-alive (한 연결로 두 요청) ---'
hdr=$(curl -s -D - -o /dev/null "$BASE/api/flights" | cat -v)
case "$hdr" in *"Connection: keep-alive"*) echo "PASS  keep-alive 헤더";; *) echo "FAIL  keep-alive 헤더"; fail=1;; esac
codes=$(curl -s -o /dev/null -o /dev/null -w '%{http_code} %{http_code}\n' \
    "$BASE/api/flights" "$BASE/api/flights/KE701")
# curl은 URL마다 -w를 한 번씩 출력하므로 앞의 두 코드만 확인한다.
set -- $codes
if [ "$1" = "200" ] && [ "$2" = "200" ]; then echo "PASS  동일 연결 2회 요청 -> $1 $2"
else echo "FAIL  동일 연결 2회 요청 -> $codes"; fail=1; fi

echo '--- slow client ---'
if command -v nc >/dev/null 2>&1; then
    (printf 'GET / HTTP/1.1\r\nHost: x\r\n'; sleep 3) | nc 127.0.0.1 8080 >/dev/null 2>&1 &
    NC=$!
    sleep 0.3
    slow=$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 "$BASE/api/flights")
    if [ "$slow" = "200" ]; then echo "PASS  slow client가 다른 요청을 막지 않음"
    else echo "FAIL  slow client 차단 -> $slow"; fail=1; fi
    kill $NC 2>/dev/null || true
else
    echo 'SKIP  nc 없음'
fi

echo '--- 출발/도착 추적 ---'
# 고정 시각으로 시드된 KE202는 날짜가 지나면 자동 도착 처리되어 상태 변경이 거부된다
# (2026-11-24 이후 실패). 그래서 현재 시각 기준으로 시드되는 미래 편 KE703과
# 현재 시각 기준 at 값을 쓴다.
NOW=$(date +%s)
expect 200 "상태 변경 DELAYED" -X POST -H 'Content-Type: application/json' \
    -d "{\"status\":\"DELAYED\",\"note\":\"기상 악화\",\"at\":$((NOW + 60))}" \
    "$BASE/api/admin/flights/KE703/status"
body=$(curl -s "$BASE/api/flights/KE703")
contains "$body" '"status":"DELAYED"' "상태 반영"
expect 400 "상태 역행 거부" -X POST -d '{"status":"SCHEDULED"}' \
    "$BASE/api/admin/flights/KE703/status"
body=$(curl -s "$BASE/api/flights/KE703/timeline")
contains "$body" "기상 악화" "타임라인 이벤트 기록"

# 100동시 예약 결과에서 201/409 개수를 센다.
race_counts() {
    seq 1 100 | xargs -P 100 -I{} curl -s -o /dev/null -w '%{http_code}\n' \
        -H 'Content-Type: application/json' -d '{"name":"user{}"}' \
        "$BASE/api/flights/KE999/reserve" | sort | uniq -c
}

echo '--- before (락 없음): 초과 예약 재현 ---'
restart_server
curl -s -o /dev/null -X POST -d '{"lock":false}' "$BASE/api/admin/mode"
counts=$(race_counts)
echo "$counts"
before_ok=$(printf '%s\n' "$counts" | awk '$2==201 {print $1}')
before_ok=${before_ok:-0}
if [ "$before_ok" -gt 1 ]; then
    echo "PASS  before: 201 ${before_ok}건 (초과 예약 재현)"
else
    echo "FAIL  before: 201 ${before_ok}건 (레이스 미재현)"; fail=1
fi

echo '--- after (뮤텍스): 정확히 1건만 성공 ---'
restart_server
curl -s -o /dev/null -X POST -d '{"lock":true}' "$BASE/api/admin/mode"
counts=$(race_counts)
echo "$counts"
after_ok=$(printf '%s\n' "$counts" | awk '$2==201 {print $1}')
after_conflict=$(printf '%s\n' "$counts" | awk '$2==409 {print $1}')
after_ok=${after_ok:-0}
after_conflict=${after_conflict:-0}
if [ "$after_ok" -eq 1 ] && [ "$after_conflict" -eq 99 ]; then
    echo "PASS  after: 201 1건 / 409 99건"
else
    echo "FAIL  after: 201 ${after_ok}건 / 409 ${after_conflict}건"; fail=1
fi

body=$(curl -s "$BASE/api/flights/KE999")
contains "$body" '"seat_left":0' "after 후 잔여 좌석 0"

echo '--- 엣지 케이스 ---'
longname=$(python3 -c 'print("a"*80)')
expect 400 "이름 64자 초과" -X POST -H 'Content-Type: application/json' \
    -d "{\"name\":\"$longname\"}" "$BASE/api/flights/KE701/reserve"
expect 404 "없는 항공편 예약" -X POST -H 'Content-Type: application/json' \
    -d '{"name":"x"}' "$BASE/api/flights/NOPE/reserve"
expect 409 "좌석 0석 예약" -X POST -H 'Content-Type: application/json' \
    -d '{"name":"zero"}' "$BASE/api/flights/KE999/reserve"
expect 200 "출발 설정" -X POST -H 'Content-Type: application/json' \
    -d "{\"status\":\"DEPARTED\",\"at\":$((NOW + 600))}" "$BASE/api/admin/flights/KE703/status"
expect 400 "도착 시각 역전" -X POST -H 'Content-Type: application/json' \
    -d "{\"status\":\"ARRIVED\",\"at\":$((NOW - 600))}" "$BASE/api/admin/flights/KE703/status"
body=$(curl -s -X POST -H 'Content-Type: application/json' \
    -d '{"name":"a\"b\\c","contact":"x"}' "$BASE/api/flights/KE701/reserve")
if printf '%s' "$body" | python3 -c 'import sys,json; d=json.load(sys.stdin); sys.exit(0 if d["reservation"]["name"]=="a\"b\\c" else 1)' 2>/dev/null; then
    echo "PASS  JSON escape (round-trip)"
else
    echo "FAIL  JSON escape: $body"; fail=1
fi

stop_server
if [ $fail -eq 0 ]; then
    echo 'flight integration: all passed'
else
    echo 'flight integration: FAILED'
fi
exit $fail
