# 동시 요청 직접 확인

[설계 목차](../design.md) · [워커 풀](workers.md) · [종료 흐름](shutdown.md)

[README](../../README.md)의 Docker 실행 방법으로 서버를 켠다. 호스트 테스트 주소는 `127.0.0.1:8080`이다. 아래 내용은 실행 방법과 기대 결과다. 실제 실행값은 [진행 상태](../progress.md)에 기록한다.

## 두 요청이 겹칠 때

Python 3가 있는 PowerShell에서 `python`을 실행하고 아래를 붙여 넣는다. 첫 연결에 HTTP 헤더 일부만 보낸 뒤, 그 연결을 연 상태로 별도 정적 요청을 보낸다.

```python
import socket, urllib.request
held = socket.create_connection(("127.0.0.1", 8080), timeout=2)
held.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
with urllib.request.urlopen("http://127.0.0.1:8080/", timeout=2) as response:
    print("별도 정적 요청:", response.status)

held.settimeout(7)
with held.makefile("rb") as result:
    print("미완료 요청:", result.readline().decode().strip())

held.close()
```

첫 줄은 미완료 요청이 열린 동안 `200`이어야 한다. 두 번째 줄은 약 5초 뒤 `HTTP/1.1 408 Request Timeout`이어야 한다. 프록시의 느린 백엔드 케이스는 [1.5 장애 가이드](../dynamic/check.md#프록시-장애-재현)를 사용해 API 요청을 보내고, 별도 터미널에서 정적 GET을 보내 응답이 먼저 오는지 확인한다.

## 워커 4개와 큐 8칸을 채웠을 때

같은 Python 창에서 새로 실행한다. 첫 연결의 5초 읽기 기한이 지나기 전에 확인할 수 있도록 전체 코드를 한 번에 실행한다.

```python
exec("""
from contextlib import ExitStack
import socket, time
with ExitStack() as stack:
    for index in range(12):
        client = stack.enter_context(socket.create_connection(("127.0.0.1", 8080), timeout=2))
        client.sendall(b"GET / HTTP/1.1\\r\\nHost: localhost\\r\\n")
        if index == 3:
            time.sleep(0.2)
    extra = stack.enter_context(socket.create_connection(("127.0.0.1", 8080), timeout=2))
    extra.sendall(b"GET / HTTP/1.1\\r\\nHost: localhost\\r\\n\\r\\n")
    reader = stack.enter_context(extra.makefile("rb"))
    print(reader.readline().decode().strip())
""")
```

기대 응답은 `503 Service Unavailable`이다. 타이밍에 따라 연결이 이미 닫혔거나 먼저 맡은 요청의 5초가 지난 경우가 있으므로 실행 결과를 그대로 기록한다. 임시 연결을 정리한 뒤 `curl.exe -i http://127.0.0.1:8080/`에서 서버가 정상 요청에 다시 답하는지 확인한다.

## 종료와 경합·부하

| 상황 | 확인할 결과 |
| --- | --- |
| 아무 요청이 없을 때 서버 터미널에서 `Ctrl+C` | 새 접속이 없어도 종료 |
| 첫 실습의 미완료 요청을 보낸 상태에서 `Ctrl+C` | 진행 중 연결을 정리하고 종료 |
| 서로 다른 터미널에서 같은 빈 좌석 예약 | 한 건 `201`, 다른 한 건 `409` |
| 정적 파일과 좌석 조회에 각각 소규모 부하를 점차 높여 실행 | 처리량·응답 지연·실패율과 과부하 관찰값 기록 |

### Docker에서 부하 측정

1. README의 기본 데모를 켜고 두 서버의 시작 로그를 확인한다.
2. 별도 PowerShell 창에서 `docker ps --filter "publish=8080" --format "{{.Names}}"`를 실행해 데모 컨테이너가 하나 나오는지 확인한다.
3. 같은 창에서 아래 명령을 한 줄씩 실행한다. 서버가 실행 중인 컨테이너의 `wrk`가 루프백으로 접속한다.

```powershell
$demo = docker ps --filter "publish=8080" --format "{{.Names}}"
docker exec $demo wrk -t2 -c4 -d10s --latency http://127.0.0.1:8080/
docker exec $demo wrk -t2 -c16 -d10s --latency http://127.0.0.1:8080/
docker exec $demo wrk -t2 -c4 -d10s --latency http://127.0.0.1:8080/api/seats
docker exec $demo wrk -t2 -c16 -d10s --latency http://127.0.0.1:8080/api/seats
```

4. 처리량, 지연, 비정상 응답·연결 오류 수를 환경과 함께 기록하고 데모 창에서 `Ctrl+C`로 종료한다. `wrk`의 기본 비정상 상태 코드 집계는 `503`만의 건수가 아니다. 상태별 수치가 필요하면 별도로 응답을 집계한다. 기존 4초 테스트의 환경과 결과는 [검증 결과](../results.md)에 있다.

### POSIX에서 SIGTERM

로컬 POSIX 환경에서 `SIGTERM`도 확인하려면 테스트용 프록시를 백그라운드에서 띄우고, 시작 로그가 나온 뒤 같은 셸에서 해당 프로세스에 종료 신호를 보낸다.

```sh
./build/web_server 18080 public 127.0.0.1 --proxy 127.0.0.1 8081 &
server_pid=$!
# 시작 로그를 확인한 뒤 다음 두 줄을 실행한다.
kill -TERM "$server_pid"
wait "$server_pid"
```
