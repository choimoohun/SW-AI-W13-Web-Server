#ifndef PROXY_H
#define PROXY_H

#include <stddef.h>

#include "csapp.h"
#include "http_request.h"

#define PROXY_MAX_ROUTES 16
#define PROXY_MAX_BACKENDS 16
#define PROXY_DEFAULT_TIMEOUT_MS 5000
#define PROXY_PREFIX_MAX 256
#define PROXY_HOST_MAX 256
#define PROXY_PORT_MAX 16
#define PROXY_HEAD_BUF (MAXLINE + 1024)

typedef struct {
    char host[PROXY_HOST_MAX]; /* IPv6도 대괄호 없이 저장 */
    char port[PROXY_PORT_MAX];
    struct sockaddr_storage addr; /* 시작할 때 한 번 해석한 주소 */
    socklen_t addrlen;
    int family, socktype, protocol;
} proxy_backend;

typedef struct {
    char prefix[PROXY_PREFIX_MAX]; /* "/" 외에는 끝 '/'를 뗀 접두사 */
    size_t prefix_len;
    proxy_backend backends[PROXY_MAX_BACKENDS];
    size_t nbackends;
    size_t next;          /* 다음에 고를 백엔드. lock으로 보호 */
    pthread_mutex_t lock;
} proxy_route;

typedef struct {
    proxy_route routes[PROXY_MAX_ROUTES];
    size_t nroutes;
    int timeout_ms;
} proxy_router;

/*
 * router를 라우트 0개로 초기화합니다. timeout_ms가 0 이하면 PROXY_DEFAULT_TIMEOUT_MS를 씁니다.
 * 실패하지 않으며 할당하는 자원은 없습니다.
 */
void proxy_router_init(proxy_router *router, int timeout_ms);

/*
 * 등록된 모든 라우트의 뮤텍스를 정리합니다. 다른 스레드가 router를 쓰지 않을 때 부릅니다. 반환값은 없습니다.
 */
void proxy_router_destroy(proxy_router *router);

/*
 * "<prefix>=<host:port>[,<host:port>...]" 규칙을 파싱하고 각 백엔드 주소를 getaddrinfo()로 해석해 라우트로 등록합니다.
 * prefix는 '/'로 시작하고 공백·제어 문자가 없어야 하며, 이미 등록된 접두사와 겹치면 안 됩니다.
 * IPv6는 "[::1]:8001"로 씁니다. 포트는 1..65535입니다.
 * 성공하면 0, 실패하면 -1을 반환하고 errbuf(errsize바이트)에 원인을 NUL로 끝내 씁니다. 실패하면 라우터는 바뀌지 않습니다.
 * 성공하면 라우트의 뮤텍스를 만들며 proxy_router_destroy()가 정리합니다.
 */
int proxy_router_add_spec(proxy_router *router, const char *spec, char *errbuf, size_t errsize);

/*
 * uri와 경로 조각 경계에서 일치하는 라우트 중 접두사가 가장 긴 것을 반환합니다.
 * 접두사 "/api"는 "/api", "/api/x", "/api?q"에 일치하고 "/apiv2"에는 일치하지 않습니다. "/"는 모든 uri에 일치합니다.
 * 없으면 NULL입니다. 라우터를 바꾸지 않고 할당하는 자원도 없습니다.
 */
proxy_route *proxy_router_match(proxy_router *router, const char *uri);

/*
 * route->lock을 잡고 라운드로빈으로 다음 백엔드를 고릅니다. 스레드 안전합니다.
 * 반환값은 route 안의 포인터이므로 해제하지 않습니다.
 */
const proxy_backend *proxy_route_next(proxy_route *route);

/*
 * 백엔드로 보낼 요청 헤더를 out(out_size바이트)에 NUL로 끝내 씁니다.
 * 요청 줄은 "<method> <uri> HTTP/1.0"이고, raw_head(http_read_head()가 읽은 클라이언트 헤더 블록, raw_len바이트)에서
 * hop-by-hop 헤더(Connection과 그 값에 적힌 이름, Keep-Alive, Proxy-*, TE, Trailer, Transfer-Encoding, Upgrade, Expect),
 * Host, Content-Length를 빼고 나머지를 복사한 뒤 Host: <backend>, Connection: close,
 * 본문이 있거나 POST면 Content-Length: <req->content_length>를 붙입니다.
 * 성공하면 NUL을 뺀 바이트 수, 헤더 이름에 RFC 9110 token이 아닌 문자가 있거나 값에 '\r'이 있거나
 * out_size에 들어가지 않으면 -1을 반환합니다. 소켓을 쓰지 않고 할당하는 자원도 없습니다.
 */
int proxy_format_request_head(const http_request *req, const char *raw_head, size_t raw_len,
                              const proxy_backend *backend, char *out, size_t out_size);

/*
 * 백엔드 응답 헤더 블록 raw_head(raw_len바이트, 빈 줄까지)를 검사하고 클라이언트에 보낼 헤더를 out에 NUL로 끝내 씁니다.
 * 상태 줄은 "HTTP/1.0" 또는 "HTTP/1.1"과 200..599 상태 코드여야 합니다. hop-by-hop 헤더를 빼고
 * Content-Length는 하나로 합친 뒤 Connection: close를 붙입니다.
 * 성공하면 NUL을 뺀 바이트 수를 반환하고 *status_out에 상태 코드, *has_cl에 Content-Length 유무(0/1),
 * *cl_out에 그 값을 저장합니다. 상태 줄 형식 오류, 1xx, Transfer-Encoding, 잘못되거나 값이 다른 Content-Length,
 * 잘못된 헤더 이름, out_size 초과면 -1을 반환합니다(호출자가 502를 보냄). 할당하는 자원은 없습니다.
 */
int proxy_filter_response_head(const char *raw_head, size_t raw_len, char *out, size_t out_size, int *status_out,
                               int *has_cl, size_t *cl_out);

/*
 * route에서 라운드로빈으로 백엔드를 골라 req를 전달하고 응답을 connfd로 중계합니다.
 * raw_head, raw_len은 http_read_head()가 읽은 클라이언트 헤더 블록이고 req->body는 이미 읽혀 있어야 합니다.
 * 연결·송신·응답 헤더 수신에 router->timeout_ms 시간 제한을 겁니다. 헤더를 보내기 전 실패는
 * 연결 거부·형식 오류면 502, 시간 초과면 504, 요청 헤더 이름 오류면 400을 connfd로 보내고 stderr에 한 줄 기록합니다.
 * 응답을 끝까지 보냈거나 오류 응답을 보냈으면 0, 클라이언트 송신 실패나 응답 헤더를 보낸 뒤 중단되면 -1을 반환합니다.
 * 백엔드 소켓은 반환 전에 닫고, connfd는 닫지 않습니다.
 */
int proxy_serve(int connfd, const proxy_router *router, proxy_route *route, const http_request *req,
                const char *raw_head, size_t raw_len);

#endif /* PROXY_H */
