#!/bin/sh
# 검토 과제용 테스트 케이스 (TC1~TC4).
#
# TC1 트래픽 급증       : 동시 100 × 1000 요청, Non-2xx 0
# TC2 느린 클라이언트    : 한 연결이 요청을 덜 본낸 채 붙어 있어도 다른 요청은 정상
# TC3 예약·결제 일관성   : 결제 실패 시 좌석 복원 (별도 백엔드 필요 → 정의만)
# TC4 워커 장애 격리     : 워커 죽어도 서비스 지속 (미구현 → SKIP)
#
# 실행: make review-cases
set -u
export FLIGHT_DATA_FILE=/dev/null
export WEB_PORT=${REVIEW_PORT:-8089}
PORT=$WEB_PORT
BASE="http://127.0.0.1:$PORT"
LOG=/tmp/jg_review_$$.log
fail=0
skip=0

./webserver >"$LOG" 2>&1 &
SRV=$!
trap '[ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null || true' EXIT

i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/health" && break
    i=$((i + 1)); sleep 0.2
done

echo "=== TC1 트래픽 급증 (c=100, n=1000) ==="
START=$(date +%s%N)
seq 1 1000 | xargs -P 100 -I{} curl -s -o /dev/null -w '%{http_code}\n' \
    --max-time 10 "$BASE/health" > /tmp/jg_tc1_$$.txt
END=$(date +%s%N)
MS=$(( (END - START) / 1000000 ))
OK=$(grep -c '^200$' /tmp/jg_tc1_$$.txt || true)
BAD=$((1000 - OK))
echo "성공(200): $OK / Non-2xx: $BAD / 소요: ${MS}ms"
rm -f /tmp/jg_tc1_$$.txt
if [ "$BAD" -ne 0 ]; then
    echo 'TC1 FAIL'; fail=$((fail + 1))
else
    echo 'TC1 PASS'
fi

echo
echo "=== TC2 느린 클라이언트가 다른 요청을 막지 않는지 ==="
python3 - "$PORT" <<'PY' >/dev/null 2>&1 &
import socket, sys, time
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port))
s.sendall(b"GET /health HTTP/1.1\r\nHost: x\r\n")   # 헤더 일부만 보내고 대기
time.sleep(6)
s.close()
PY
SLOW=$!
sleep 0.5
T0=$(date +%s%N)
CODE=$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$BASE/health" || echo 000)
T1=$(date +%s%N)
ELAPSED=$(( (T1 - T0) / 1000000 ))
kill "$SLOW" >/dev/null 2>&1 || true
wait "$SLOW" 2>/dev/null || true
echo "느린 연결이 붙어 있는 동안 /health = $CODE (${ELAPSED}ms)"
if [ "$CODE" = "200" ] && [ "$ELAPSED" -lt 3000 ]; then
    echo 'TC2 PASS'
else
    echo 'TC2 FAIL'; fail=$((fail + 1))
fi

echo
echo "=== TC3 예약·결제 일관성 ==="
cat <<'TXT'
정의: 예약으로 좌석이 줄었는데 결제가 실패하면 좌석이 복원되어야 한다.
재현 입력 (게이트웨이 8085 + 백엔드 8500):
  1) tests/backends/serve_payments.py 실행
  2) conf/backends.conf로 8085 서버 기동
  3) POST /api/flights/<id>/book  → 좌석 -1
  4) POST /api/payments/ 실패 유도 (금액 불일치)
  5) GET  /api/flights/<id>      → 좌석이 원래대로 복원됐는지 확인
기대: 결제 실패 시 좌석 복원. 현재는 결제가 별도 백엔드라 실패 신호를
      예약 저장소에 돌려주는 경로가 없음 → 미구현.
TXT
echo 'TC3 NOT IMPLEMENTED'; skip=$((skip + 1))

echo
echo "=== TC4 워커 장애 격리 ==="
cat <<'TXT'
정의: 워커 하나가 죽어도 나머지 워커가 요청을 계속 처리해야 한다.
재현 입력 (멀티프로세스 기준):
  1) ./webserver &   # 마스터 + N개 워커
  2) kill -9 <worker_pid>
  3) curl http://127.0.0.1:<port>/health → 200
기대: 워커 수가 줄었더라도 서비스는 계속.
현재 구조는 단일 프로세스 + 워커 스레드라 프로세스 격리가 없음 → 미구현.
TXT
echo 'TC4 NOT IMPLEMENTED'; skip=$((skip + 1))

echo
echo "요약: FAIL=$fail, NOT IMPLEMENTED=$skip"
rm -f "$LOG"
exit $fail
