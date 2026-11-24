# HTTP 요청 파싱 함수 명세

[design.md](./design.md)의 `http_request.c`에 들어가는 함수의 역할, 입력, 출력입니다.

- **입력**: 인자와, 함수가 읽는 외부 상태(소켓에 도착한 바이트, `rio_t` 버퍼에 남은 바이트)입니다.
- **출력**: 반환값과 부수 효과(채우는 구조체, 할당하는 메모리)입니다.

## 반환값 상수

```c
enum {
    HTTP_READ_OK = 0,      /* 요청을 끝까지 읽음 */
    HTTP_READ_CLOSED = -1, /* EOF 또는 읽기 오류. 응답 없이 연결을 닫음 */
    HTTP_READ_BAD = 400,   /* 형식 오류나 크기 초과. 400을 보내고 연결을 닫음 */
    HTTP_READ_NOMEM = 500  /* 본문 버퍼 할당 실패. 500을 보내고 연결을 닫음 */
};
```

`HTTP_READ_BAD`와 `HTTP_READ_NOMEM`을 상태 코드와 같은 값으로 둔 이유는 호출자가 그 값을 그대로 [`http_send_error()`](../http-response/functions.md#http_responsec)에 넘길 수 있기 때문입니다.

## http_request.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `int http_read_head(rio_t *rp, char *buf, size_t size, size_t *len)` | `rio_readlineb()`로 한 줄씩 읽어 빈 줄까지 `buf`에 이어 붙입니다. 각 줄이 `\r\n`으로 끝나는지 확인합니다. `req`는 건드리지 않습니다. | `rp`: 연결 소켓으로 초기화한 `rio_t`. `buf`, `size`: 헤더 블록을 담을 버퍼와 크기(`MAXLINE`). 소켓에 도착한 바이트 | `HTTP_READ_OK`이면 `buf`에 빈 줄까지 포함한 헤더 블록과 끝 NUL, `*len`에 NUL을 뺀 바이트 수. 헤더 블록이 버퍼에 들어가지 않거나 줄이 `\n` 앞에 `\r`이 없으면 `HTTP_READ_BAD`. 빈 줄 전에 EOF(줄 중간의 EOF 포함)나 오류면 `HTTP_READ_CLOSED`. 판정 순서는 아래 [세부 규칙](#http_read_head) |
| `int http_parse_head(const char *buf, size_t len, http_request *req)` | 헤더 블록을 파싱해 `req`를 채웁니다. 첫 줄을 요청 줄로, 나머지를 `이름: 값` 헤더로 읽습니다. 소켓을 쓰지 않는 순수 함수입니다. | `buf`, `len`: `http_read_head()`가 만든 헤더 블록. `req`: 채울 구조체 | 성공하면 0. 형식 오류면 400, 지원하지 않는 HTTP 버전이면 505. 판정 기준은 [design.md의 판정 규칙](./design.md#판정-규칙). 실패하면 `req`의 다른 필드는 정의되지 않음. `req->body`는 쓰지 않음 |
| `int http_read_body(rio_t *rp, http_request *req)` | `req->content_length`가 0보다 크면 그만큼 `malloc()`하고 같은 `rp`로 `rio_readnb()`를 호출해 본문을 읽습니다. | `rp`: `http_read_head()`에 쓴 그 `rio_t`. `req`: 파싱이 끝난 요청 | `HTTP_READ_OK`이면 `req->body`에 본문(NUL로 끝나지 않음). `content_length`가 0이면 할당하지 않음. `malloc()` 실패면 `HTTP_READ_NOMEM`. 읽은 바이트가 `content_length`보다 적으면(EOF) 또는 오류면 `HTTP_READ_CLOSED`. 이때 할당한 `req->body`는 해제하지 않고 `http_request_free()`에 맡김 |
| `void http_request_free(http_request *req)` | `req->body`를 해제하고 `NULL`로 둡니다. 본문 메모리를 해제하는 유일한 함수입니다. | `req`: `{0}`으로 초기화한 뒤 위 함수들을 거친 구조체 | 반환 없음. `body`가 `NULL`이어도 안전 |

## 세부 규칙

### http_read_head

한 줄마다 다음 순서로 판정합니다. `rem = size - 누적 길이`입니다.

1. `rem < 3`이면 `\r\n`과 NUL을 담을 공간이 없으므로 `HTTP_READ_BAD`입니다. `rio_readlineb()`는 `maxlen`이 1이면 읽지 않고 0을 반환하므로, 이 검사가 없으면 상한에 걸린 요청이 EOF로 오인됩니다.
2. `rio_readlineb(rp, buf + 누적, rem)`을 호출합니다. 0이나 -1이면 `HTTP_READ_CLOSED`입니다.
3. 읽은 줄이 `\n`으로 끝나지 않으면, 반환값이 `rem - 1`이면 버퍼가 찬 것이므로 `HTTP_READ_BAD`, 더 작으면 줄 중간에 EOF가 온 것이므로 `HTTP_READ_CLOSED`입니다.
4. `\n` 앞이 `\r`이 아니면 `HTTP_READ_BAD`입니다.
5. 줄이 `\r\n` 두 바이트뿐이면 `HTTP_READ_OK`, 아니면 1로 돌아갑니다.

이 순서 하나로 긴 URI와 긴 헤더 블록을 함께 막습니다.
- 줄 안의 NUL 바이트도 그대로 이어 붙입니다. 길이는 `rio_readlineb()`의 반환값으로 계산하고 `strlen()`을 쓰지 않습니다.
- 빈 줄을 만난 뒤에는 더 읽지 않습니다. 빈 줄 뒤에 이미 도착한 본문은 `rp`의 내부 버퍼에 남아 `http_read_body()`가 꺼냅니다.

### http_parse_head

- 요청 줄은 공백 하나씩으로 나뉜 토큰 세 개입니다. 토큰이 빠지거나, 넷 이상이거나, 빈 토큰(연속 공백)이 있으면 400입니다.
- `method`는 토큰 문자(영문자, 숫자, ``!#$%&'*+-.^_`|~``)만 허용합니다. 대소문자는 바꾸지 않으므로 `get`도 파싱은 성공합니다. 어떤 메서드를 처리할지는 이 함수가 아니라 [연결 처리](../static-file-serving/design.md#연결-처리-흐름)가 정합니다. 따라서 `DELETE`도 파싱은 성공합니다.
- `uri`는 `/`로 시작해야 합니다.
- `version`은 `HTTP/1.0` 또는 `HTTP/1.1`이면 성공, `HTTP/`로 시작하는 다른 값이면 505, 그 밖에는 400입니다.
- 헤더 블록에 NUL 바이트가 하나라도 있으면 400입니다. `http_read_head()`는 NUL을 그대로 넘기지만 문자열 필드에 복사하면 NUL 뒤가 잘리기 때문입니다.
- 헤더 줄에 `:`이 없거나 `:` 앞의 이름이 비어 있으면 400입니다.
- 헤더 이름은 길이를 확인한 뒤 `strncasecmp()`로 `Host`, `Content-Length`, `Connection`과 비교합니다. 버퍼의 이름은 NUL로 끝나지 않기 때문입니다. 값은 `:` 뒤의 공백과 줄 끝 공백을 잘라 저장합니다. 나머지 헤더는 형식만 확인하고 버립니다.
- `Content-Length`는 숫자로만 이루어져야 하며 `HTTP_MAX_BODY`를 넘으면 400입니다. 부호, 공백, 빈 값은 400입니다. 여러 번 오면 값이 모두 같아야 하고, 다르면 400입니다.
- `Transfer-Encoding` 헤더가 있으면 값과 관계없이 501입니다.
- 값이 필드 크기보다 길면(`method` 15바이트, `version` 15바이트, `connection` 31바이트 초과) 400입니다.
- 문자열 복사는 `snprintf()`나 길이를 확인한 `memcpy()`로 합니다. `strcpy()`, `sprintf()`는 쓰지 않습니다.

### http_read_body

- 래퍼 `Rio_readnb()`가 아니라 `rio_readnb()`를 호출합니다. 반환값이 `content_length`보다 작으면 클라이언트가 본문을 다 보내기 전에 연결을 닫은 것입니다.
- 크기 상한은 `http_parse_head()`가 이미 확인했으므로 이 함수는 확인하지 않습니다.
