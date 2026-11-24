#!/bin/sh
# 누수 검사 — Docker 컨테이너(Linux) 내부에서 실행되는 스크립트.
# Makefile 'leak' 타겟이 docker compose run 으로 이 스크립트를 호출한다.
# 호스트 macOS .o 혼입을 피하려고 /tmp/ws 복사본에서 빌드한 뒤 valgrind 로 검사.
set -u
mkdir -p /tmp/ws
cp -r /workspace/src /workspace/include /workspace/www /workspace/Makefile /tmp/ws/
cd /tmp/ws
make clean >/dev/null 2>&1
make || { echo "BUILD FAILED"; exit 1; }

echo "=== valgrind leak check (정적 파일 시나리오) ==="
valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=99 ./webserver &
VG=$!
i=0
while [ $i -lt 40 ]; do
    curl -s -o /dev/null --max-time 1 http://127.0.0.1:8080/ && break
    i=$((i+1)); sleep 0.5
done
curl -s -o /dev/null -w "GET /      -> %{http_code}\n" http://127.0.0.1:8080/
curl -s -o /dev/null -w "nope       -> %{http_code}\n" http://127.0.0.1:8080/nope
curl -s -o /dev/null -w "traversal  -> %{http_code}\n" --path-as-is http://127.0.0.1:8080/../etc/passwd
curl -s -o /dev/null -w "png        -> %{http_code}\n" http://127.0.0.1:8080/test.png
curl -s -o /dev/null -w "POST       -> %{http_code}\n" -X POST -d x http://127.0.0.1:8080/
sleep 1
kill -TERM $VG
wait $VG
rc=$?
echo "valgrind exit: $rc  (0 = 누수/오류 없음)"
exit $rc
