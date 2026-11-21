#!/bin/sh
# 정적 파일 서빙 기능(통합) 테스트 — 마일스톤 1~4.
# 서버를 띄우고 curl 로 검증한 뒤 종료한다. 요구사항 §7: 정적 200/403/404, POST, 바이너리.
# 실행: sh tests/integration/run.sh   (프로젝트 루트에서, ./webserver 빌드 후)
set -u
PORT=8080
BASE="http://127.0.0.1:$PORT"

./webserver &
SRV=$!
trap 'kill -TERM $SRV 2>/dev/null; wait $SRV 2>/dev/null' EXIT
# 서버 준비 대기(ASAN 빌드는 시작이 느릴 수 있음) — 최대 ~10초 폴링
i=0
while [ $i -lt 50 ]; do
    curl -s -o /dev/null --max-time 1 "$BASE/" && break
    i=$((i+1)); sleep 0.2
done

fail=0
code() { curl -s -o /dev/null -w "%{http_code}" "$@"; }
expect() { # expect <expected> <desc> <curl args...>
    exp="$1"; desc="$2"; shift 2
    got=$(code "$@")
    if [ "$got" = "$exp" ]; then echo "PASS  $desc -> $got"; else echo "FAIL  $desc -> $got (expected $exp)"; fail=1; fi
}

expect 200 "GET /"            "$BASE/"
expect 200 "GET /index.html"  "$BASE/index.html"
expect 200 "GET /style.css"   "$BASE/style.css"
expect 200 "GET /sample.txt"  "$BASE/sample.txt"
expect 404 "GET /nope"        "$BASE/nope"
expect 403 "GET /../etc/passwd"      --path-as-is "$BASE/../etc/passwd"
expect 403 "GET /%2e%2e/etc/passwd"  --path-as-is "$BASE/%2e%2e/etc/passwd"
expect 405 "POST / (body)"    -X POST -d x "$BASE/"

# 바이너리 무결성: 받은 PNG 가 원본과 바이트 일치
if curl -s "$BASE/test.png" | cmp -s - www/test.png; then
    echo "PASS  binary integrity (test.png)"
else
    echo "FAIL  binary integrity (test.png)"; fail=1
fi

# MIME 확인
ct=$(curl -s -D - -o /dev/null "$BASE/test.png" | tr -d '\r' | awk -F': ' 'tolower($1)=="content-type"{print $2}')
if [ "$ct" = "image/png" ]; then echo "PASS  MIME image/png"; else echo "FAIL  MIME -> $ct"; fail=1; fi

if [ "$fail" = 0 ]; then echo "integration: all passed"; else echo "integration: FAILED"; fi
exit $fail
