#!/bin/sh
# Linux(epoll) 경로 전체 회귀 검증 — Docker 컨테이너 내부에서 실행.
set -u
mkdir -p /tmp/wsl
cp -r /workspace/src /workspace/include /workspace/www /workspace/conf \
      /workspace/tests /workspace/Makefile /tmp/wsl/
cd /tmp/wsl
make clean >/dev/null 2>&1
make || { echo "BUILD FAILED"; exit 1; }

fail=0
for t in test integration test-flight integration-flight integration-persist \
         test-tracking integration-tracking integration-req \
         integration-payments load-test; do
    echo "===== $t ====="
    make "$t" || { echo "FAILED: $t"; fail=1; }
done

echo "===== backend detect ====="
grep -q 'IOMUX_BACKEND_EPOLL' /tmp/wsl/src/iomux.c && echo "epoll backend source present"
if [ $fail -eq 0 ]; then echo 'linux regression: all passed'; fi
exit $fail
