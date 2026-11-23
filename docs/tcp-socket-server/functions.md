# TCP 소켓 서버 함수 명세

[design.md](./design.md)에서 나눈 파일 중 직접 작성하는 함수의 역할, 입력, 출력입니다. 동작 기준은 CS:APP 3판 11.4.9입니다.

- **입력**: 인자와, 함수가 읽는 외부 상태(표준 입력, 소켓에 도착한 바이트)입니다.
- **출력**: 반환값과 부수 효과(표준 출력과 표준 오류에 쓰는 내용, 소켓에 보내는 바이트, 디스크립터 생성과 해제)입니다.

## csapp.c에서 가져다 쓰는 것

`csapp.h`, `csapp.c`는 공식 파일을 그대로 복사하므로 명세를 따로 두지 않습니다. 각 함수의 동작은 교재 10.5절(RIO)과 11.4.7~11.4.8절(`open_clientfd`, `open_listenfd`)을 봅니다. 구현과 테스트에 직접 영향을 주는 성질만 적습니다.

- 대문자로 시작하는 래퍼(`Accept`, `Rio_writen` 등)는 실패하면 메시지를 출력하고 `exit(0)`으로 프로세스를 끝냅니다. 실패해도 종료 코드가 0이므로 **성공 여부는 종료 코드가 아니라 표준 오류 메시지로 판단합니다.**
- `rio_readlineb()`는 `\n`을 만나거나 `maxlen - 1`바이트를 채울 때까지 블로킹합니다. 반환값은 읽은 바이트 수, 데이터 없이 EOF면 0, 오류면 -1입니다.
- `rio_writen()`은 성공하면 `n`, 실패하면 -1(`EPIPE`, `ECONNRESET` 등)을 반환합니다.
- `rio_read()`와 `rio_writen()`은 `EINTR`이면 다시 시도합니다.

## echo.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `void echo(int connfd)` | 연결 하나를 담당합니다. `rio_t`와 `char buf[MAXLINE]`을 지역 변수로 두고 `Rio_readinitb()`로 초기화한 뒤, `Rio_readlineb()`가 0을 반환할 때까지 읽은 `n`바이트를 그대로 `Rio_writen()`합니다. | `connfd`: `Accept()`가 반환한 연결 소켓. 클라이언트가 보낸 바이트 스트림 | 반환 없음. 줄마다 표준 출력에 `server received n bytes`, 클라이언트에게 받은 바이트와 같은 `n`바이트 송신. 클라이언트가 FIN을 보내 EOF가 되면 반환. `connfd`는 닫지 않음 |

- 한 번에 읽는 줄은 최대 `MAXLINE - 1` = 8191바이트입니다. 더 긴 줄은 여러 번에 나뉘어 출력과 송신이 일어나지만 전체 바이트는 그대로 돌아갑니다.
- 줄 안의 NUL 바이트도 `n`에 포함해 그대로 보냅니다. 문자열 함수가 아니라 바이트 수로 쓰기 때문입니다.
- **보완 후**: 래퍼 대신 `rio_readlineb()`, `rio_writen()`을 호출하고, -1이면 표준 오류에 원인을 출력한 뒤 반환합니다. 서버는 그 연결만 닫고 계속 동작합니다.

## echoserveri.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `int main(int argc, char **argv)` | 리스닝 소켓을 만들고 무한 반복으로 연결을 하나씩 수락합니다. 연결마다 `clientlen = sizeof(struct sockaddr_storage)`로 다시 설정하고 `Accept()`, `Getnameinfo()`, `echo()`, `Close()` 순서로 처리합니다. | `argv[1]`: 포트 문자열 | 인자 개수가 2가 아니면 표준 오류에 `usage: <argv[0]> <port>` 후 `exit(0)`. 연결마다 표준 출력에 `Connected to (host, port)`. 정상 경로에서는 반환하지 않음 |

- `struct sockaddr_storage`는 IPv4와 IPv6 주소를 모두 담을 수 있는 크기라서 프로토콜과 무관하게 `Accept()`에 넘깁니다.
- `Getnameinfo()`의 `flags`가 0이므로 `host`는 역방향 조회 결과(로컬 접속이면 보통 `localhost`), `port`는 클라이언트의 임시 포트 번호입니다.
- **보완 후**: 시작할 때 `Signal(SIGPIPE, SIG_IGN)`을 호출합니다. `Accept()` 대신 `accept()`를 호출하고, `EINTR`이나 `ECONNABORTED`이면 다음 반복으로 넘어갑니다.

## echoclient.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `int main(int argc, char **argv)` | 서버에 연결한 뒤, 표준 입력에서 `Fgets()`로 한 줄을 읽어 `Rio_writen()`으로 보내고, `Rio_readlineb()`로 응답 한 줄을 받아 `Fputs()`로 출력합니다. 표준 입력이 EOF가 되면 `Close()` 후 `exit(0)`합니다. | `argv[1]`: 호스트, `argv[2]`: 포트. 표준 입력의 줄 | 인자 개수가 3이 아니면 표준 오류에 `usage: <argv[0]> <host> <port>`. 정상이면 받은 줄을 표준 출력에 그대로 씀 |

- 보낸 줄에 `\n`이 없으면 서버는 줄 끝을 기다리고 클라이언트는 응답 줄을 기다려 둘 다 멈춥니다.
- 서버 응답이 줄 하나보다 길게 나뉘면 클라이언트는 한 번에 한 줄만 읽으므로 출력이 다음 입력 뒤로 밀릴 수 있습니다.
