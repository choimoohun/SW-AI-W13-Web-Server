# HTTP 응답 생성 함수 명세

[design.md](./design.md)의 `http_response.c`에 들어가는 함수의 역할, 입력, 출력입니다.

- **입력**: 인자입니다.
- **출력**: 반환값과 부수 효과(버퍼에 쓰는 바이트, 소켓에 보내는 바이트)입니다.

## http_response.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `const char *http_status_text(int status)` | 상태 코드의 상태 메시지를 돌려줍니다. | `status`: [design.md의 상태 코드](./design.md#상태-코드) 표의 코드 | 표에 있으면 상태 메시지 문자열 상수(`"Not found"` 등), 없으면 `NULL` |
| `int http_format_head(char *buf, size_t size, int status, const char *content_type, size_t content_length)` | 응답 줄, `Content-Type`, `Content-Length`, `Connection: close`, 빈 줄을 `snprintf()` 한 번으로 씁니다. 소켓을 쓰지 않는 순수 함수입니다. | `buf`, `size`: 쓸 버퍼와 크기. `status`, `content_type`, `content_length`: 헤더 값 | 성공하면 NUL을 뺀 바이트 수. 상태 메시지가 없거나 `snprintf()` 결과가 `size` 이상(잘림)이면 -1 |
| `int http_send_head(int fd, int status, const char *content_type, size_t content_length)` | `MAXLINE` 크기 지역 버퍼에 `http_format_head()`로 쓰고 `rio_writen()`으로 보냅니다. | `fd`: 연결 소켓. 나머지는 `http_format_head()`와 같음 | 성공 0. 형식 실패나 송신 실패(`EPIPE`, `ECONNRESET` 등)면 -1 |
| `int http_send_error(int fd, int status)` | 아래 [오류 본문](#오류-본문)을 만들고 `http_send_head(fd, status, "text/html", 본문 길이)` 뒤 본문을 `rio_writen()`으로 보냅니다. | `fd`: 연결 소켓. `status`: 4xx 또는 5xx 코드 | 성공 0, 실패 -1. 연결은 닫지 않음 |

## 헤더 형식

`http_format_head()`가 쓰는 바이트는 정확히 다음과 같습니다. 줄 끝은 모두 `\r\n`입니다.

```c
"HTTP/1.1 %d %s\r\n"
"Content-Type: %s\r\n"
"Content-Length: %zu\r\n"
"Connection: close\r\n"
"\r\n"
```

## 오류 본문

```c
"<html><body><h1>%d %s</h1></body></html>\n"
```

`%d`는 상태 코드, `%s`는 상태 메시지입니다. 본문 길이는 `snprintf()`의 반환값으로 정하고 `strlen()`으로 다시 세지 않습니다. 본문은 ASCII만 쓰므로 글자 수와 바이트 수가 같습니다. 예를 들어 404의 본문은 `<html><body><h1>404 Not found</h1></body></html>\n`, 49바이트입니다.

## 주의할 점

- `http_send_error()`는 연결을 닫지 않습니다. 닫는 일은 [연결 처리](../static-file-serving/design.md#연결-처리-흐름)가 한 곳에서 합니다.
- `http_send_head()`의 버퍼 `MAXLINE`(8192)은 헤더 다섯 줄에 충분합니다. `content_type`은 [MIME 표](../static-file-serving/functions.md#mime-타입-표)의 상수만 들어오므로 길이가 정해져 있습니다.
