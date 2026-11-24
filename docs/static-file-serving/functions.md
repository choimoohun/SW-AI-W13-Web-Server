# 정적 파일 서빙 함수 명세

[design.md](./design.md)의 `static_file.c`와 `httpd.c`에 들어가는 함수의 역할, 입력, 출력입니다.

- **입력**: 인자와, 함수가 읽는 외부 상태(파일 시스템, 소켓에 도착한 바이트)입니다.
- **출력**: 반환값과 부수 효과(소켓에 보내는 바이트, 디스크립터 생성과 해제, 표준 출력)입니다.

## MIME 타입 표

| 확장자 | MIME 타입 |
| --- | --- |
| `html` | `text/html` |
| `css` | `text/css` |
| `js` | `text/javascript` |
| `txt` | `text/plain` |
| `png` | `image/png` |
| `jpg`, `jpeg` | `image/jpeg` |
| `gif` | `image/gif` |
| 그 밖, 확장자 없음 | `application/octet-stream` |

확장자는 경로의 마지막 `/` 뒤에서 마지막 `.` 다음 부분입니다. `strcasecmp()`로 비교하므로 `A.PNG`도 `image/png`입니다. `js`는 RFC 9239의 권고대로 `text/javascript`를 씁니다.

## static_file.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `const char *mime_type(const char *path)` | 위 표에서 확장자에 맞는 MIME 타입을 찾습니다. 순수 함수입니다. | `path`: 파일 경로 | MIME 타입 문자열 상수. `NULL`을 반환하지 않음 |
| `int static_resolve_path(const char *root, const char *uri, char *path, size_t size)` | [design.md의 URL 접미사 해석](./design.md#url-접미사-해석) 다섯 단계로 디스크 경로를 만듭니다. 파일 시스템에 접근하지 않는 순수 함수입니다. | `root`: CLI로 받은 루트. `uri`: `http_parse_head()`가 검사한 `/`로 시작하는 URI. `path`, `size`: 결과 버퍼(`MAXLINE`) | 성공 0, `path`에 경로. `..` 조각이면 403, 결과가 `size`를 넘으면 400 |
| `int static_serve(int fd, const char *path)` | 파일을 `O_RDONLY \| O_NONBLOCK`으로 열어 [상태 코드 판정](./design.md#상태-코드-판정)을 한 뒤, 200이면 `http_send_head()`와 본문을 보내고, 아니면 `http_send_error()`를 보냅니다. 연 파일은 반환 전에 닫습니다. | `fd`: 연결 소켓. `path`: `static_resolve_path()`의 결과. 파일 시스템 | 응답을 끝까지 보냈으면 0. 송신 실패(`EPIPE` 등), 본문 전송 중 `read()` 실패, `Content-Length`만큼 읽기 전의 EOF면 -1. 어느 경우든 연결은 닫지 않음 |

### static_serve의 본문 전송

```c
char buf[MAXLINE];
off_t left = sb.st_size;    /* Content-Length로 보낸 크기 */
while (left > 0) {
    size_t want = left < (off_t)sizeof buf ? (size_t)left : sizeof buf;
    ssize_t n = read(filefd, buf, want);
    if (n < 0 && errno == EINTR)
        continue;
    if (n <= 0)
        return -1;          /* 헤더를 이미 보냈으므로 연결을 끊음 */
    if (rio_writen(fd, buf, (size_t)n) != n)
        return -1;
    left -= n;
}
```

설명을 위해 `filefd`를 닫는 코드는 뺐습니다. 실제 구현에서는 모든 반환 경로에서 `close(filefd)`를 호출합니다. 크기가 0인 파일은 반복문을 한 번도 돌지 않고, 헤더(`Content-Length: 0`)만 보냅니다.

본문은 `fstat()`으로 얻은 크기만큼만 보냅니다. 헤더를 보낸 뒤 다른 프로세스가 파일을 늘리면 늘어난 부분은 보내지 않습니다. 파일이 줄어 `Content-Length`만큼 읽기 전에 EOF가 오면 -1을 반환하고, 호출자가 연결을 닫아 클라이언트는 본문이 잘렸음을 알 수 있습니다.

## httpd.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `int main(int argc, char **argv)` | `SIGPIPE`를 무시하고 리스닝 소켓을 만든 뒤, 연결마다 `handle_connection()`과 `Close()`를 호출합니다. `accept()`가 `EINTR`, `ECONNABORTED`로 실패하면 다음 반복으로 넘어갑니다. | `argv[1]`: 포트, `argv[2]`: 정적 루트 | 인자 개수가 3이 아니면 표준 오류에 `usage: <argv[0]> <port> <root>` 후 `exit(0)`. 정상 경로에서는 반환하지 않음 |
| `static void handle_connection(int connfd, const char *root)` | [연결 처리 흐름](./design.md#연결-처리-흐름)대로 요청 하나를 읽고 응답 하나를 보냅니다. `http_request`와 `rio_t`는 지역 변수이며 `http_request req = {0};`로 초기화합니다. | `connfd`: 연결 소켓. `root`: 정적 루트 | 반환 없음. 파싱에 성공하면 표준 출력에 `<method> <uri> <version>` 한 줄. 어느 경로로 반환하든 그 전에 `http_request_free()`를 한 번 호출. `connfd`는 닫지 않음 |

- `handle_connection()`은 하위 함수의 -1을 따로 처리하지 않습니다. 어느 경우든 반환하면 `main()`이 연결을 닫기 때문입니다.
- 요청 처리 경로에서는 `Rio_*` 래퍼를 쓰지 않습니다. 실패하면 서버 프로세스 전체가 끝나기 때문입니다. `Open_listenfd()`, `Close()`, `Signal()`처럼 서버 시작과 연결 정리에 쓰는 래퍼는 [TCP 소켓 서버](../tcp-socket-server/functions.md#echoserveric)와 같이 씁니다.
