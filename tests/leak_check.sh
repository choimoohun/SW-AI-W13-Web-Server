#!/bin/sh
# 누수 검사 — Docker 컨테이너(Linux) 내부에서 실행되는 스크립트.
# Makefile 'leak' 타겟이 docker compose run 으로 이 스크립트를 호출한다.
# 호스트 macOS .o 혼입을 피하려고 /tmp/ws 복사본에서 빌드한 뒤 valgrind 로 검사.
set -u
mkdir -p /tmp/ws
cp -r /workspace/src /workspace/include /workspace/www /workspace/conf \
      /workspace/Makefile /tmp/ws/
cd /tmp/ws
make clean >/dev/null 2>&1
make || { echo "BUILD FAILED"; exit 1; }

echo "=== valgrind leak check (전체 코드 경로) ==="
FLIGHT_DATA_FILE=/dev/null \
valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=99 \
    ./webserver -c conf/tcpserver.conf >/tmp/vg.out 2>&1 &
VG=$!
i=0
while [ $i -lt 40 ]; do
    curl -s -o /dev/null --max-time 1 http://127.0.0.1:8080/health && break
    i=$((i+1)); sleep 0.5
done

# 정적 + CDN(hit/miss) + 캐시 헤더
curl -s -o /dev/null -w "index       -> %{http_code}\n" http://127.0.0.1:8080/
curl -s -o /dev/null -w "index again -> %{http_code}\n" http://127.0.0.1:8080/
curl -s -o /dev/null -w "png         -> %{http_code}\n" http://127.0.0.1:8080/test.png
curl -s -o /dev/null -w "traversal   -> %{http_code}\n" --path-as-is http://127.0.0.1:8080/../etc/passwd
curl -s -o /dev/null -w "POST        -> %{http_code}\n" -X POST -d x http://127.0.0.1:8080/

# 내장 엔드포인트 + 항공편 저장소/진행 경로
curl -s -o /dev/null -w "health      -> %{http_code}\n" http://127.0.0.1:8080/health
curl -s -o /dev/null -w "echo        -> %{http_code}\n" -X POST -d hi http://127.0.0.1:8080/echo
curl -s -o /dev/null -w "flights     -> %{http_code}\n" http://127.0.0.1:8080/api/flights
curl -s -o /dev/null -w "reserve     -> %{http_code}\n" -X POST -d '{"name":"vg"}' http://127.0.0.1:8080/api/flights/KE701/reserve
curl -s -o /dev/null -w "timeline    -> %{http_code}\n" http://127.0.0.1:8080/api/flights/KE701/timeline
curl -s -o /dev/null -w "ops clock   -> %{http_code}\n" http://127.0.0.1:8080/api/ops/clock

# 프록시 스레드(백엔드 없음 → 502)까지 실행
curl -s -o /dev/null -w "proxy 502   -> %{http_code}\n" http://127.0.0.1:8080/api/cache/x

sleep 1
kill -TERM $VG
wait $VG
rc=$?
echo "--- valgrind 요약 ---"
grep -E 'HEAP SUMMARY|in use at exit|All heap blocks|ERROR SUMMARY' /tmp/vg.out || true
echo "--- 첫 오류 상세 ---"
grep -n -B35 -A5 'ERROR SUMMARY' /tmp/vg.out | head -80 || true
echo "valgrind exit: $rc  (0 = 누수/오류 없음)"
exit $rc
