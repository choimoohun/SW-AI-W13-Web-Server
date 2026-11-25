# 동시 요청 처리 테스트 케이스

[design.md](./design.md)대로 만든 스레드 풀과 종료 절차가 맞는지 확인하는 최소 테스트입니다.

## 준비

[동적 요청 처리 테스트의 준비](../dynamic-request/test-cases.md#준비)에 이어 2초 걸리는 CGI와 자기 디스크립터를 출력하는 CGI를 만듭니다. 터미널 A에서 `./build/httpd 15213 /tmp/www`로 서버를 실행합니다.

```sh
cat > /tmp/www/cgi-bin/slow <<'EOF'
#!/bin/sh
sleep 2
printf 'Content-Type: text/plain\r\n\r\nslow done\n'
EOF
cat > /tmp/www/cgi-bin/fds <<'EOF'
#!/bin/sh
printf 'Content-Type: text/plain\r\n\r\n'
ls -l /proc/$$/fd
EOF
chmod 755 /tmp/www/cgi-bin/slow /tmp/www/cgi-bin/fds
```

`fds`는 CGI 셸이 물려받은 디스크립터를 `/proc`에서 읽으므로 Linux(Docker 포함)에서만 동작합니다. 다른 프로세스를 찾을 필요가 없어 `pgrep`(`procps`)은 쓰지 않습니다.

## 단위 테스트

`tests/test_conn_queue.c`에 작성하고 CTest에 `test_conn_queue`로 등록합니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| C01 | 1, 2, 3을 넣고 세 번 꺼냄 | 1, 2, 3 순서 |
| C02 | 7을 넣고 닫은 뒤 두 번 꺼냄 | 7, 그다음 -1. 닫혀도 남은 연결은 꺼냄 |
| C03 | 빈 대기열에서 `pop`하는 스레드를 띄우고 0.1초 뒤 닫음 | 그 스레드가 깨어나 -1을 받고 끝남 |
| C04 | 다른 스레드가 17개를 넣는 동안 0.1초 기다림 | `count`가 16에서 멈춤. 하나 꺼내면 마지막 push가 끝나고 0~16이 순서대로 나옴 |

## 기능 테스트

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| C05 | 터미널 B에서 `(printf 'POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nhello'; sleep 30) \| nc localhost 15213`을 띄운 채 터미널 C에서 `curl -s -o /dev/null -w '%{http_code} %{time_total}\n' http://localhost:15213/` | 곧바로 `200`. 4단계 S16은 5초 기다렸음 |
| C06 | `/cgi-bin/slow`에 `curl` 8개를 동시에 보내고 전체 시간을 잼 | 8개 모두 `slow done`, 약 2초. 순차라면 16초 |
| C07 | `wrk -t4 -c50 -d3s http://localhost:15213/` | `Socket errors`, `Non-2xx` 없음 |
| C08 | `/cgi-bin/slow`에 `curl`을 보내고 0.5초 뒤 `kill -TERM <서버 pid>` | `curl`이 `slow done`을 받고, 서버가 종료 코드 0으로 끝남 |
| C09 | C05의 `nc`를 띄워 다른 손님의 연결을 열어 둔 채 `curl -s http://localhost:15213/cgi-bin/fds` | 0(`/dev/null`), 1(파이프), 2와 셸이 스크립트를 읽으려고 연 파일만 있음. 소켓(`socket:[...]`)이 없음 |
| C10 | Debug 빌드에 `-fsanitize=address,undefined`를 켜고 C05~C08 | sanitizer 보고 없음. 종료 시 LeakSanitizer 보고 없음 |

C06은 C05의 느린 연결이 워커 하나를 잡고 있는 동안 돌리면 워커 7개로 8개를 처리하므로 약 4초가 걸립니다. 따로 실행합니다.
