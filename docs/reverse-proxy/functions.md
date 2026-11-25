# 리버스 프록시 함수 명세

[design.md](./design.md)의 `proxy.c`와 `httpd.c` 변경에 들어가는 함수의 역할, 입력, 출력입니다. 1차 구현 범위만 적습니다.

## proxy.c 공개 함수

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `void proxy_router_init(proxy_router *router, int timeout_ms)` | 라우트 0개로 초기화합니다. `timeout_ms <= 0`이면 `PROXY_DEFAULT_TIMEOUT_MS`(5000). | 없음 |
| `void proxy_router_destroy(proxy_router *router)` | 등록된 라우트의 뮤텍스를 정리합니다. 워커가 모두 끝난 뒤 부릅니다. | 없음 |
| `int proxy_router_add_spec(proxy_router *router, const char *spec, char *errbuf, size_t errsize)` | `<prefix>=<host:port>[,<host:port>...]`를 파싱합니다. 접두사는 `/`로 시작하고 공백·제어 문자가 없어야 하며 `/` 외에는 끝 `/`를 뗍니다. 백엔드는 `host:port` 또는 `[v6]:port`, 포트 1..65535이고 `getaddrinfo()`로 한 번 해석합니다. 라우트 16개, 라우트당 백엔드 16개까지입니다. | 성공 0. 형식 오류, 중복 접두사, 용량 초과, 해석 실패면 -1과 `errbuf`의 원인. 실패해도 라우터는 바뀌지 않음 |
| `proxy_route *proxy_router_match(proxy_router *router, const char *uri)` | 접두사 뒤가 `\0`, `/`, `?`인 라우트 중 가장 긴 것을 고릅니다. `/`는 모든 URI에 일치합니다. | 라우트 포인터, 없으면 `NULL` |
| `const proxy_backend *proxy_route_next(proxy_route *route)` | `route->lock` 아래에서 `next`를 하나 올리며 백엔드를 돌아가며 고릅니다. | 라우트 안의 백엔드 포인터 |
| `int proxy_format_request_head(const http_request *req, const char *raw_head, size_t raw_len, const proxy_backend *backend, char *out, size_t out_size)` | `<method> <uri> HTTP/1.0` 줄을 쓰고, 클라이언트 헤더에서 hop-by-hop(정적 목록 + `Connection` 값에 적힌 이름 + `Expect`), `Host`, `Content-Length`를 뺀 줄을 복사한 뒤 `Host`, `Connection: close`, 필요하면 `Content-Length`와 빈 줄을 붙입니다. | 성공하면 NUL을 뺀 바이트 수. 헤더 이름이 token이 아니거나 값에 HTAB 외 제어 문자가 있거나 URI에 제어 문자가 있거나 버퍼가 모자라면 -1 |
| `int proxy_filter_response_head(const char *raw_head, size_t raw_len, char *out, size_t out_size, int *status_out, int *has_cl, size_t *cl_out)` | 상태 줄(`HTTP/1.0`·`HTTP/1.1`, 200..599)을 검사해 그대로 복사하고, hop-by-hop을 뺀 헤더, 하나로 합친 `Content-Length`, `Connection: close`, 빈 줄을 씁니다. | 성공하면 바이트 수와 상태 코드, `Content-Length` 유무·값. 1xx, `Transfer-Encoding`, 잘못되거나 값이 다른 `Content-Length`, 헤더 값의 HTAB 외 제어 문자, 형식 오류면 -1 |
| `int proxy_serve(int connfd, const proxy_router *router, proxy_route *route, const http_request *req, const char *raw_head, size_t raw_len)` | 백엔드 고르기 → 요청 헤더 만들기(실패 시 400) → `proxy_connect` → 헤더·본문 송신 → `http_read_head()`로 응답 헤더 읽기 → 검사 → 헤더 송신 → 본문 중계. 헤더를 보내기 전 실패는 400/502/504를 보내고 `stderr`에 한 줄 남깁니다. | 응답을 끝까지 보냈거나 오류 응답을 보냈으면 0, 클라이언트 송신 실패나 헤더 송신 뒤 중단이면 -1. 백엔드 소켓은 닫고 `connfd`는 닫지 않음 |

## proxy.c static 함수

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `parse_backend` | `host:port` 하나를 검사하고 `getaddrinfo(AF_UNSPEC, SOCK_STREAM, AI_NUMERICSERV)`에서 `AF_INET` 주소를 먼저, 없으면 첫 주소를 복사합니다. 괄호 없는 호스트에 `:`가 있으면 거부합니다. | 0 또는 -1 |
| `listed_in_connection` | 헤더 블록의 모든 `Connection:` 줄에서 쉼표·공백으로 나눈 토큰 중 이름이 같은 것이 있는지 봅니다(대소문자 무시). | 1 또는 0 |
| `filter_headers` | 요청·응답 공통 헤더 순회. 남길 줄만 `out`에 붙입니다. 응답이면 `Content-Length`를 모으고 `Transfer-Encoding`을 거부합니다. | 0 또는 -1 |
| `proxy_connect` | `socket` → `FD_CLOEXEC` → `O_NONBLOCK` → `connect` → `poll(POLLOUT)`으로 마감 시각까지 대기 → `SO_ERROR` 확인 → 블로킹 복원 → `SO_RCVTIMEO`/`SO_SNDTIMEO`. | 소켓 fd. 실패하면 -1과 상태(시간 초과 504, 그 밖 502) |
| `stream_body` | RIO 버퍼에 남은 본문을 먼저 보내고 8 KiB씩 `read` → `rio_writen`. `Content-Length`가 있으면 그만큼만 보내고 EOF를 기다리지 않습니다. | 0 또는 -1 |

## httpd.c 변경

| 함수 | 변경 | 출력 |
| --- | --- | --- |
| `main` | `argc < 3`(`<port>`나 `<root>`가 없음)이면 usage 후 `exit(1)`. `argv[3..]`의 `--route <spec>`을 `proxy_router_add_spec()`으로 등록하고, 모르는 인자·값 누락·규칙 오류면 원인을 출력하고 `exit(1)`. 종료할 때 워커를 기다린 뒤 `proxy_router_destroy()`. | 신호로 끝나면 0 |
| `handle_connection` | 본문을 읽은 뒤 `static_has_bad_segments()`면 403 → `proxy_router_match()` → 허용 메서드(GET, 프록시와 `/cgi-bin/`은 POST도) 아니면 501 → 라우트가 있으면 `proxy_serve()`, 없으면 기존 CGI/정적 분기. | 없음 |

## static_file.c 변경

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `int static_has_bad_segments(const char *uri)` | `static_resolve_path()` 안에 있던 경로 조각 검사를 떼어 공개했습니다. `?` 앞 경로에서 정확히 `..`, `.`, 빈 조각을 찾습니다. | 있으면 1, 없으면 0 |
