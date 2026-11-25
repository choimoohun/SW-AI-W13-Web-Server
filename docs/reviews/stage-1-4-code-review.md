# 1~4단계 코드 리뷰

1~4단계(TCP 소켓 서버부터 정적 파일 서빙까지)의 구현이 CS:APP 11.6 Tiny의 약점을 해결했는지, 그리고 반복형 서버에서 무엇이 남았는지 확인한 리뷰다. 정적 파일 서버로는 정상 동작하고 Tiny의 정적 서빙 약점은 모두 해결되어 있다. 남은 문제 중 먼저 고칠 것은 **FIFO 요청과 느린 클라이언트가 서버 전체를 멈추는 것**이다. 둘 다 클라이언트 하나가 반복형 서버 전체를 막는다. 설계 선택의 근거는 각 단계의 `design.md`([요청 파싱](../http-request-parsing/design.md), [응답 생성](../http-response/design.md), [정적 파일 서빙](../static-file-serving/design.md))에 있다.

| 항목 | 내용 |
| --- | --- |
| 저장소 | [SW-AI-W13-Web-Server](https://github.com/choimoohun/SW-AI-W13-Web-Server/tree/choimoohun/main) |
| 브랜치, 커밋 | `choimoohun/main`, `21816d7` |
| 빌드 | `-Wall -Wextra -fsanitize=address,undefined`에서 직접 작성한 코드의 경고 0건. 경고 4건은 교재 `csapp.c`에서만 나옴 |
| 단위 테스트 | `ctest` 3/3 통과 (`test_http_request`, `test_http_response`, `test_static_file`) |
| 통합 확인 | `httpd 8123 ./www`를 띄우고 `nc`와 Python 소켓으로 직접 요청 |

## 동작 확인 결과

루트 `./www` 안에 `index.html`, 디렉터리 `sub/`, FIFO `pipe`를 두고, 루트 바깥에 `secret.txt`를 두었다. 요청 8개 중 5개는 기대대로 응답했고, 1개는 상태 코드가 덜 정확했으며, FIFO 요청이 서버를 멈춰 뒤의 정상 요청도 응답을 받지 못했다.

| 요청 | 응답 | 판정 |
| --- | --- | --- |
| `GET / HTTP/1.1` + `Host` | 200, `index.html` 12바이트, `text/html` | 정상 |
| `GET /../secret.txt HTTP/1.0` | 403 | 정상, 경로 탐색 차단 |
| `GET /sub HTTP/1.0` (디렉터리) | 403 | 정상 |
| `get / HTTP/1.0` (소문자 메서드) | 400 | 501이 더 정확 (수정할 점 3) |
| `POST /` + `Content-Length: 5` + 본문 | 501, 본문까지 읽은 뒤 응답 | 정상 |
| 20,000바이트 헤더 줄 | 400 | 정상 |
| `GET /pipe HTTP/1.0` (FIFO) | 응답 없음, 3초 타임아웃 | 결함 (수정할 점 1) |
| FIFO 요청 직후의 `GET /` | 응답 없음 | 결함, 서버가 FIFO에서 멈춰 있음 |

따로 실험한 결과도 있다. `Content-Length: 10`에 본문 5바이트만 보내고 연결을 유지하면, 그 클라이언트가 연결을 닫을 때까지 다른 클라이언트가 모두 기다린다(수정할 점 2).

## Tiny의 약점을 해결한 부분

| Tiny의 약점 | 이 구현의 처리 | 위치 |
| --- | --- | --- |
| 헤더 도중 EOF면 `read_requesthdrs`가 무한 루프 | `rio_readlineb` 반환값이 0 이하이면 `HTTP_READ_CLOSED` | `http_read_head` |
| 경로 탐색(`/../`) 방어 없음 | 정확히 `..`인 경로 조각만 403으로 거부하고 `a..b`는 허용 | `static_resolve_path` |
| `sprintf(buf, "%s...", buf)` 겹침 복사(정의되지 않은 동작) | `snprintf`와 잘림 검사 | `http_format_head`, `http_send_error` |
| SIGPIPE 기본 동작과 대문자 `Rio_writen` 래퍼가 프로세스 종료 | `Signal(SIGPIPE, SIG_IGN)`과 소문자 `rio_writen` 반환값 검사 | `main`, `http_send_head` |
| `Accept` 래퍼가 일시적 오류에도 종료 | `EINTR`, `ECONNABORTED`면 다시 시도 | `main` |
| `stat`과 `open` 사이의 틈 | `open` 뒤 같은 fd에 `fstat` | `static_serve` |
| 파일이 바뀌면 `Content-length`와 실제 전송량이 어긋날 수 있음 | `fstat` 크기만큼만 보내고, 파일이 줄면 연결을 끊음 | `static_serve` |
| 헤더 크기 제한 없음 | 헤더 블록이 `MAXLINE`을 넘거나 줄 끝에 `\r`이 없으면 400 | `http_read_head` |

`open` 뒤 `fstat` 순서는 Tiny보다 나은 설계다. 검사한 파일과 보내는 파일이 같다는 것이 보장된다.

헤더와 본문을 같은 `rio`로 읽는 것도 맞다. 헤더를 읽을 때 본문이 이미 `rio_buf`에 들어와 있을 수 있으므로, `read(connfd, ...)`로 따로 읽으면 교착이 생긴다. 본문이 `Content-Length`보다 짧게 오면 `HTTP_READ_CLOSED`로 처리하는 검사도 있다.

## 수정할 점

| 번호 | 문제 | 우선순위 | 확인 방법 |
| --- | --- | --- | --- |
| 1 | FIFO 같은 특수 파일 요청에서 서버 전체 정지 | 높음 | 직접 재현 |
| 2 | 느린 클라이언트 하나가 서버 전체를 막음 | 높음 | 실험으로 확인 |
| 3 | 모르는 메서드에 501 대신 400 | 중간 | 직접 재현 |
| 4 | `Content-Length` 중복, `Transfer-Encoding` 무시 | 지금은 낮음, 프록시에서는 높음 | 코드 검토 |
| 5 | 오류 응답 직후 `close`로 인한 RST 가능성 | 참고 | 재현하지 못함 |

1, 3, 4번은 수정했습니다. 2, 5번과 리뷰 뒤 발견한 `*` 형식 요청 대상은 방법이 여럿이라 [미룬 결정](./stage-1-4-deferred-decisions.md)에 장단점과 제안을 정리했습니다.

### 1. FIFO 요청에서 서버 전체 정지

- 위치: `static_file.c`의 `static_serve`
- 원인: FIFO를 `open(path, O_RDONLY)`로 열면 쓰는 쪽이 나타날 때까지 막힌다. `S_ISREG` 검사는 `open` 다음에 있어 도달하지 못한다. 반복형 서버라 이후 요청도 모두 멈춘다.
- 재현: `mkfifo www/pipe` 뒤 `printf 'GET /pipe HTTP/1.0\r\n\r\n' | nc localhost 8123`. 이어서 보낸 `GET /`도 응답이 없다.
- 수정 방향: `O_NONBLOCK`으로 열고 `fstat`으로 일반 파일인지 확인한 뒤, 플래그를 되돌리고 읽는다. POSIX에서 `O_RDONLY | O_NONBLOCK`으로 FIFO를 열면 쓰는 쪽이 없어도 곧바로 반환한다.

```c
int filefd = open(path, O_RDONLY | O_NONBLOCK);
/*... fstat 뒤 !S_ISREG면 403... */
int fl = fcntl(filefd, F_GETFL);
fcntl(filefd, F_SETFL, fl & ~O_NONBLOCK);
```

- 추가할 테스트: FIFO 요청에 403, 그 직후의 `GET /`에 200

### 2. 느린 클라이언트가 서버 전체를 막음

- 위치: `httpd.c`의 `handle_connection` (`http_read_head`, `http_read_body`, 응답 전송)
- 원인: 연결이 열려 있고 수신 버퍼가 비면 `read`가 막힌다. EOF(0)는 연결이 닫혀야 온다. 응답을 읽지 않는 클라이언트라면 송신 버퍼가 차서 `rio_writen`도 막힌다.
- 재현: `Content-Length: 10`에 본문 5바이트만 보내고 연결 유지. 다른 클라이언트가 모두 기다린다.
- 임시 대책: `accept` 직후 `connfd`에 시간 제한을 건다. 제한을 넘기면 `read`가 -1과 `EAGAIN`을 반환하고, RIO는 `EINTR`일 때만 다시 시도하므로 `HTTP_READ_CLOSED`로 처리된다.

```c
struct timeval tv = {.tv_sec = 5,.tv_usec = 0 };
setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
setsockopt(connfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
```

- 한계: 제한 시간은 `read`, `write` 호출 하나마다 따로 적용된다. 4초마다 1바이트씩 보내는 클라이언트는 막지 못한다. 근본 해결은 CS:APP 12장의 동시성이다.
- 추가할 테스트: 헤더를 반만 보내고 멈춘 클라이언트가 있을 때, 두 번째 클라이언트가 제한 시간과 여유 안에 200을 받는지

### 3. 모르는 메서드에 501 대신 400

- 위치: `http_request.c`의 `parse_request_line`. 메서드 글자를 `A`~`Z`로만 허용한다.
- 현상: `get / HTTP/1.0`과 `M-SEARCH * HTTP/1.1`이 400이 된다.
- 근거: RFC 9110에서 메서드는 대소문자를 구분하는 토큰이다. `get`은 GET의 오타가 아니라 모르는 메서드이고, 모르거나 구현하지 않은 메서드에는 501이 권장된다. Tiny는 `strcasecmp`로 비교해 `get`도 GET으로 처리하지만, 표준 기준으로는 501이 맞다.
- 수정 방향: 메서드 검사를 토큰 문자(영문자, 숫자, ``!#$%&'*+-.^_`|~``) 검사로 바꾸고 형식이 깨진 경우만 400으로 한다. GET이 아니면 501을 보내는 분기는 `handle_connection`에 이미 있다.
- 추가할 테스트: `get` → 501, `M-SEARCH` → 501, `G(T` → 400

### 4. `Content-Length` 중복과 `Transfer-Encoding` 무시

- 위치: `http_request.c`의 `parse_header_line`
- 현상: `Content-Length`가 여러 번 오면 마지막 값을 쓴다. `Transfer-Encoding: chunked`는 무시되어 본문 길이가 0이 된다.
- 지금 해가 없는 이유: 응답 뒤 항상 연결을 닫으므로 다음 요청으로 번질 바이트가 없다.
- 프록시에서 위험한 이유: 프록시와 뒤쪽 서버가 본문 경계를 다르게 판단하면 요청 밀반입(request smuggling)이 가능해진다.
- 근거: RFC 9112는 잘못되었거나 서로 다른 값이 함께 온 `Content-Length`를 복구할 수 없는 오류로 보고 400 뒤 연결 종료를 요구한다. 이해하지 못하는 전송 코딩에는 501을 권장한다.
- 수정 방향: 두 번째 `Content-Length`가 다른 값이면 400. chunked를 구현하기 전까지는 `Transfer-Encoding`이 있으면 501.
- 추가할 테스트: `Content-Length: 5` 줄과 `Content-Length: 6` 줄이 함께 오면 400, `Transfer-Encoding: chunked` → 501

### 5. 오류 응답 직후 `close`로 인한 RST 가능성

- 위치: `handle_connection`에서 400을 보낸 뒤 곧바로 `Close`
- 내용: 읽지 않은 입력이 수신 버퍼에 남은 채 소켓을 닫으면 리눅스는 FIN 대신 RST를 보낼 수 있다. 그러면 클라이언트가 400 응답을 읽기 전에 연결 리셋 오류를 볼 수 있다.
- 관찰: 20,000바이트 헤더 시험에서는 루프백이라 400을 정상 수신했고, 재현하지 못했다.
- 대책 방향: `shutdown(connfd, SHUT_WR)` 뒤 남은 입력을 짧은 시간 동안 읽어 버리고 닫는다(lingering close).

## 다음 단계 설계

> 이 절은 리뷰 당시의 제안입니다. 실제 구현은 [동적 요청 처리](../dynamic-request/design.md)와 [리버스 프록시](../reverse-proxy/design.md) 설계를 따릅니다. 예를 들어 CGI 본문은 입력 파이프가 아니라 `mkstemp()` 임시 파일로 넘깁니다.

### CGI

Tiny처럼 `200 OK`를 먼저 보내지 않고, CGI 출력을 파이프로 받아 자식의 종료 상태를 본 뒤 상태 코드를 정한다. HTTP 상태 줄은 맨 앞에 오고, 이미 보낸 바이트는 되돌릴 수 없기 때문이다. 아래는 아직 구현하지 않은 제안 순서다.

1. 파이프 두 개를 만든다. 하나는 CGI 표준 입력용, 하나는 표준 출력용이다.
2. `fork` 뒤 자식은 `QUERY_STRING`, `REQUEST_METHOD`, `CONTENT_LENGTH` 등 환경 변수를 설정하고, 파이프를 fd 0과 1에 `dup2`한다. 쓰지 않는 fd(특히 `connfd`와 파이프의 반대쪽 끝)를 닫은 뒤 `execve`한다.
3. 부모는 `http_read_body`가 같은 `rio`로 읽어 둔 `req->body`를 입력 파이프에 쓰고 닫는다. `connfd`를 CGI의 fd 0으로 직접 넘기면 `rio_buf`에 들어온 본문을 CGI가 받지 못한다.
4. 부모는 출력 파이프를 EOF까지 읽어 메모리에 모은다. 크기 상한을 둔다.
5. `waitpid`로 자식을 회수하고, 정상 종료면 200과 모은 출력을, 아니면 500을 보낸다.

3번에서 본문이 파이프 버퍼보다 크고 CGI가 입력을 다 읽기 전에 출력부터 쓰면, 부모와 자식이 서로 기다리는 교착이 생길 수 있다. 입력 크기를 제한하거나 읽기와 쓰기를 `poll`로 번갈아 처리한다.

### 프록시

- 수정할 점 4번(`Content-Length` 충돌, `Transfer-Encoding`)을 먼저 해결한다.
- 수정할 점 2번의 막힘은 프록시에서 두 배가 된다. 클라이언트 쪽 연결과 뒤쪽 서버 쪽 연결이 모두 느릴 수 있으므로 동시성(CS:APP 12장)을 먼저 갖춘다.
- 뒤쪽 서버의 응답도 같은 원칙으로 읽는다. 헤더는 빈 줄까지, 본문은 `Content-Length` 바이트 수만큼 같은 `rio`로 읽는다.
