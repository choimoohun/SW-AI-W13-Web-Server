# TCP 소켓 서버 테스트 케이스

[design.md](./design.md)대로 만든 순차 에코 서버가 TCP 소켓 서버 요구사항을 만족하는지 확인하는 테스트 명세입니다. 기대 결과는 [functions.md](./functions.md)의 입력과 출력을 따릅니다.

## 준비

Docker에서는 같은 컨테이너에 터미널을 여러 개 붙입니다. `docker compose run`을 터미널마다 실행하면 컨테이너가 따로 생겨 `localhost`로 서로 접속할 수 없습니다.

```sh
# 터미널 A: 컨테이너 시작
docker compose run --rm --name web-dev dev sh

# 터미널 B: 같은 컨테이너에 접속
docker exec -it web-dev sh
```

개발 이미지에 없는 `nc`와 `python3`를 컨테이너 안에서 설치하고 빌드합니다.

```sh
apt-get update && apt-get install -y netcat-openbsd python3
cmake -S . -B build
cmake --build build
```

터미널 A에서 `./build/echoserveri 15213`으로 서버를 실행해 두고, 터미널 B에서 클라이언트를 실행합니다. 래퍼 오류도 `exit(0)`으로 끝나므로 종료 코드가 아니라 출력과 서버 생존 여부로 판정합니다.

## 연결과 송수신

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| T01 | `./build/echoclient localhost 15213` 후 `hello` 입력 | 클라이언트에 `hello` 출력. 서버에 `Connected to (localhost, <임시 포트>)`와 `server received 6 bytes` |
| T02 | T01 클라이언트를 `Ctrl+D`로 끝내고 다시 연결해 `again` 전송 | 서버가 새 연결을 수락하고 `server received 6 bytes`. EOF 뒤 `Close()`하고 `Accept()`로 돌아감 |
| T03 | `printf 'a\nb\nc\n' \| nc -N localhost 15213` | `nc`에 `a`, `b`, `c` 세 줄. 한 번에 도착한 여러 줄을 줄마다 처리 |
| T04 | `(printf 'hel'; sleep 1; printf 'lo\n'; sleep 1) \| nc -N localhost 15213` | 나뉘어 도착한 줄을 모아 `server received 6 bytes` 한 번, `nc`에 `hello` |
| T05 | `python3 -c "print('a'*10000)" \| nc -N localhost 15213 \| wc -c` | `10001`. 버퍼(8192)보다 긴 줄도 바이트 손실 없이 돌아옴 |

## 리스닝 소켓

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| T06 | 서버 실행 중에 터미널 B에서 `./build/echoserveri 15213` | 두 번째 서버가 `Open_listenfd error: Address already in use` 후 종료. 첫 서버는 계속 동작 |
| T07 | 클라이언트가 연결된 상태에서 서버를 `Ctrl+C`로 끝내고 곧바로 다시 실행 | `SO_REUSEADDR` 덕분에 바로 바인딩 성공 |

## 순차 처리

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| T08 | 클라이언트 1을 연결만 하고 대기. 다른 터미널의 클라이언트 2에서 `second` 입력 후 클라이언트 1을 `Ctrl+D` | 클라이언트 1이 끝날 때까지 클라이언트 2는 응답을 받지 못함. 끝나면 서버가 클라이언트 2를 수락하고 `second`를 돌려줌 |

## 비정상 종료

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| T09 | 연결한 `echoclient`를 다른 터미널에서 `kill -9` | 커널이 FIN을 보내므로 서버는 해당 연결을 닫고 다음 연결을 기다림 |
| T10 | 아래 스크립트로 RST를 보내며 끊은 뒤 T01 실행 | 서버가 표준 오류에 `rio_readlineb error: Connection reset by peer` 또는 `rio_writen error: ...`를 쓰고 해당 연결만 닫음. T01 통과 |

```sh
python3 -c "
import socket, struct
s = socket.create_connection(('127.0.0.1', 15213))
s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
s.sendall(b'x\n')
s.close()
"
```

`SO_LINGER`를 0초로 두고 닫으면 FIN 대신 RST가 나갑니다. T10은 [design.md의 보완한 지점](./design.md#보완한-지점)을 확인합니다. 보완 전 교재 코드로 실행하면 서버가 `Rio_readlineb error: Connection reset by peer`, `Rio_writen error: ...`를 출력하거나 메시지 없이 `SIGPIPE`로 끝나 T01이 `Connection refused`가 됩니다. RST가 언제 도착하느냐에 따라 메시지 없이 정상 EOF로 끝날 수도 있습니다.

## 메모리

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| T11 | 앞에서 띄운 서버를 `Ctrl+C`로 끝냄(남아 있으면 valgrind 서버가 T06처럼 `Address already in use`로 끝나고 T01, T02가 기존 서버에 붙어 검사가 무의미해짐). Debug 빌드(`cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug`) 후 `valgrind --leak-check=full ./build-debug/echoserveri 15213`, T01과 T02를 실행하고 `Ctrl+C` | 잘못된 읽기와 쓰기 없음. `definitely lost: 0 bytes` |

## 범위 밖 항목

HTTP 처리, 정적 파일, 동시 접속, 부하 테스트, 프록시는 교재 11.5 이후와 12장 구현에서 테스트합니다. 위 케이스는 수동 기능 테스트이며, CTest 자동화는 HTTP 단계에서 추가합니다.
