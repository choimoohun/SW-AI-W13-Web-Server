/* test_proxy.c - proxy.c 단위·루프백 테스트 (docs/reverse-proxy/test-cases.md) */
#undef NDEBUG
#include <assert.h>
#include <time.h>

#include "proxy.h"

static char err[512];

static int add(proxy_router *r, const char *spec)
{
    return proxy_router_add_spec(r, spec, err, sizeof err);
}

static void test_spec(void)
{
    proxy_router r;
    proxy_router_init(&r, 0);
    assert(r.timeout_ms == PROXY_DEFAULT_TIMEOUT_MS);

    /* P01: 정상 규칙, 끝 '/' 정규화, IPv6 */
    assert(add(&r, "/api/=127.0.0.1:8001,localhost:8002") == 0);
    assert(strcmp(r.routes[0].prefix, "/api") == 0 && r.routes[0].nbackends == 2);
    assert(strcmp(r.routes[0].backends[1].host, "localhost") == 0);
    assert(strcmp(r.routes[0].backends[1].port, "8002") == 0);
    assert(add(&r, "/=[::1]:8003") == 0);
    assert(strcmp(r.routes[1].prefix, "/") == 0);
    assert(strcmp(r.routes[1].backends[0].host, "::1") == 0 && r.routes[1].backends[0].family == AF_INET6);

    /* P02: 잘못된 규칙은 거부하고 라우터를 바꾸지 않음 */
    const char *bad[] = {"api=127.0.0.1:1", "/x", "/api=127.0.0.1:1", "/a b=127.0.0.1:1", "/x=127.0.0.1",
                         "/x=127.0.0.1:0", "/x=127.0.0.1:65536", "/x=127.0.0.1:8a", "/x=:80", "/x=127.0.0.1:1,",
                         "/x=[::1:80", "/x=", "/x=::1", "/x=::1:80"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err[0] = '\0';
        assert(add(&r, bad[i]) == -1);
        assert(err[0] != '\0');
        assert(r.nroutes == 2);
    }

    /* P03: 백엔드 17개, 라우트 17개는 거부 */
    char spec[1024] = "/many=";
    for (int i = 0; i < 17; i++)
        strcat(spec, i ? ",127.0.0.1:1" : "127.0.0.1:1");
    assert(add(&r, spec) == -1);
    for (int i = (int)r.nroutes; i < PROXY_MAX_ROUTES; i++) {
        snprintf(spec, sizeof spec, "/r%d=127.0.0.1:1", i);
        assert(add(&r, spec) == 0);
    }
    assert(add(&r, "/one-more=127.0.0.1:1") == -1);
    proxy_router_destroy(&r);
}

static void test_match(void)
{
    proxy_router r;
    proxy_router_init(&r, 0);
    assert(proxy_router_match(&r, "/api") == NULL);
    assert(add(&r, "/api=127.0.0.1:1") == 0);
    assert(add(&r, "/api/v2=127.0.0.1:2") == 0);

    /* P04: 경로 조각 경계와 최장 일치 */
    proxy_route *api = &r.routes[0], *v2 = &r.routes[1];
    assert(proxy_router_match(&r, "/api") == api);
    assert(proxy_router_match(&r, "/api/") == api);
    assert(proxy_router_match(&r, "/api/a") == api);
    assert(proxy_router_match(&r, "/api?x=1") == api);
    assert(proxy_router_match(&r, "/api/v2") == v2);
    assert(proxy_router_match(&r, "/api/v2/x?y") == v2);
    assert(proxy_router_match(&r, "/api/v20") == api);
    assert(proxy_router_match(&r, "/apiv2") == NULL);
    assert(proxy_router_match(&r, "/") == NULL);

    /* P05: "/"는 나머지 모두. 등록 순서와 무관하게 긴 접두사가 이김 */
    assert(add(&r, "/=127.0.0.1:3") == 0);
    assert(proxy_router_match(&r, "/apiv2") == &r.routes[2]);
    assert(proxy_router_match(&r, "/api/v2/x") == v2);
    proxy_router_destroy(&r);
}

static proxy_router rr;

static void *rr_worker(void *arg)
{
    int *counts = arg;
    for (int i = 0; i < 100; i++)
        counts[proxy_route_next(&rr.routes[0]) - rr.routes[0].backends]++;
    return NULL;
}

static void test_round_robin(void) /* P06 */
{
    proxy_router_init(&rr, 0);
    assert(add(&rr, "/=127.0.0.1:1,127.0.0.1:2,127.0.0.1:3,127.0.0.1:4") == 0);
    pthread_t tid[8];
    int counts[8][4] = {{0}};
    for (int i = 0; i < 8; i++)
        assert(pthread_create(&tid[i], NULL, rr_worker, counts[i]) == 0);
    for (int i = 0; i < 8; i++)
        pthread_join(tid[i], NULL);
    for (int b = 0; b < 4; b++) {
        int sum = 0;
        for (int i = 0; i < 8; i++)
            sum += counts[i][b];
        assert(sum == 200);
    }
    proxy_router_destroy(&rr);
}

static void make_req(http_request *req, const char *method, const char *uri, size_t cl)
{
    memset(req, 0, sizeof *req);
    snprintf(req->method, sizeof req->method, "%s", method);
    snprintf(req->uri, sizeof req->uri, "%s", uri);
    req->content_length = cl;
}

static void test_request_head(void)
{
    proxy_router r;
    proxy_router_init(&r, 0);
    assert(add(&r, "/=127.0.0.1:8001,[::1]:8002") == 0);
    const proxy_backend *v4 = &r.routes[0].backends[0], *v6 = &r.routes[0].backends[1];
    http_request req;
    char out[PROXY_HEAD_BUF];

    /* P07: 요청 줄, hop-by-hop 제거, Host 덮어쓰기, end-to-end 헤더 유지 */
    const char raw[] = "GET /api/u?a=1 HTTP/1.1\r\n"
                       "Host: client.example\r\n"
                       "Connection: keep-alive, X-Secret\r\n"
                       "Keep-Alive: timeout=5\r\n"
                       "Proxy-Connection: keep-alive\r\n"
                       "TE: trailers\r\n"
                       "Upgrade: h2c\r\n"
                       "Expect: 100-continue\r\n"
                       "X-Secret: s\r\n"
                       "Authorization: Bearer t\r\n"
                       "Accept: */*\r\n"
                       "\r\n";
    make_req(&req, "GET", "/api/u?a=1", 0);
    int n = proxy_format_request_head(&req, raw, strlen(raw), v4, out, sizeof out);
    assert(n == (int)strlen(out));
    assert(strcmp(out, "GET /api/u?a=1 HTTP/1.0\r\n"
                       "Authorization: Bearer t\r\n"
                       "Accept: */*\r\n"
                       "Host: 127.0.0.1:8001\r\n"
                       "Connection: close\r\n"
                       "\r\n") == 0);

    /* P08: Content-Length는 하나로, 빈 POST는 0, IPv6 Host는 대괄호 */
    const char post[] = "POST /x HTTP/1.1\r\nContent-Length: 3\r\nContent-Length: 3\r\nContent-Type: a/b\r\n\r\n";
    make_req(&req, "POST", "/x", 3);
    assert(proxy_format_request_head(&req, post, strlen(post), v6, out, sizeof out) > 0);
    assert(strcmp(out, "POST /x HTTP/1.0\r\nContent-Type: a/b\r\nHost: [::1]:8002\r\nConnection: close\r\n"
                       "Content-Length: 3\r\n\r\n") == 0);
    const char empty[] = "POST /x HTTP/1.1\r\n\r\n";
    make_req(&req, "POST", "/x", 0);
    assert(proxy_format_request_head(&req, empty, strlen(empty), v4, out, sizeof out) > 0);
    assert(strstr(out, "Content-Length: 0\r\n") != NULL);

    /* 이름 뒤 공백·단독 CR로 숨긴 헤더, 버퍼 부족은 -1 */
    const char sneaky[] = "GET / HTTP/1.1\r\nTransfer-Encoding : chunked\r\n\r\n";
    make_req(&req, "GET", "/", 0);
    assert(proxy_format_request_head(&req, sneaky, strlen(sneaky), v4, out, sizeof out) == -1);
    const char bare_cr[] = "GET / HTTP/1.1\r\nX-A: 1\rX-B: 2\r\n\r\n";
    assert(proxy_format_request_head(&req, bare_cr, strlen(bare_cr), v4, out, sizeof out) == -1);
    assert(proxy_format_request_head(&req, raw, strlen(raw), v4, out, 40) == -1);
    const char plain[] = "GET / HTTP/1.1\r\n\r\n";
    make_req(&req, "GET", "/x\rX-Evil: 1", 0); /* 요청 줄로 우회한 단독 CR */
    assert(proxy_format_request_head(&req, plain, strlen(plain), v4, out, sizeof out) == -1);
    proxy_router_destroy(&r);
}

static int filter(const char *raw, char *out, int *status, int *has_cl, size_t *cl)
{
    return proxy_filter_response_head(raw, strlen(raw), out, PROXY_HEAD_BUF, status, has_cl, cl);
}

static void test_response_head(void)
{
    char out[PROXY_HEAD_BUF];
    int status, has_cl;
    size_t cl;

    /* P09: 정상 응답. hop-by-hop 제거, Content-Length 하나로, Connection: close */
    assert(filter("HTTP/1.1 200 OK\r\nConnection: keep-alive, X-Hop\r\nX-Hop: 1\r\nKeep-Alive: 5\r\n"
                  "Content-Type: text/plain\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n",
                  out, &status, &has_cl, &cl) > 0);
    assert(status == 200 && has_cl == 1 && cl == 5);
    assert(strcmp(out, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\nConnection: close\r\n\r\n") ==
           0);
    assert(filter("HTTP/1.0 404 Not Found\r\n\r\n", out, &status, &has_cl, &cl) > 0);
    assert(status == 404 && has_cl == 0);
    assert(filter("HTTP/1.1 204\r\n\r\n", out, &status, &has_cl, &cl) > 0 && status == 204);
    assert(filter("HTTP/1.1 304 Not Modified\r\n\r\n", out, &status, &has_cl, &cl) > 0 && status == 304);

    /* P10: 거부 */
    const char *bad[] = {
        "HTTP/1.1 100 Continue\r\n\r\n",
        "HTTP/1.1 101 Switching\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n",
        "HTTP/2 200 OK\r\n\r\n",
        "HTTP/1.1 20 OK\r\n\r\n",
        "HTTP/1.1 2000 OK\r\n\r\n",
        "HTTP/1.1 600 X\r\n\r\n",
        "garbage\r\n\r\n",
        "HTTP/1.1 200 OK\r\nBad Name: x\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX: 1\r\n", /* 빈 줄 없음 */
        "HTTP/1.1 200\rInjected: 1\r\n\r\n",    /* 상태 줄 안의 단독 CR */
        "HTTP/1.1 200 OK\rInjected: 1\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        assert(filter(bad[i], out, &status, &has_cl, &cl) == -1);
}

/* ---- 루프백 ---- */

typedef struct {
    int listenfd;
    const char *resp; /* 연결마다 보낼 응답 */
    size_t resp_len;
    int delay_ms;  /* 응답 전 대기 */
    int hold_ms;   /* 응답 뒤 닫기 전 대기 */
    int nconns;    /* 받을 연결 수 */
    char req[16384]; /* 마지막으로 받은 요청 */
    size_t req_len;
} backend;

static void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
    nanosleep(&ts, NULL);
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *backend_main(void *arg)
{
    backend *b = arg;
    for (int i = 0; i < b->nconns; i++) {
        int fd = accept(b->listenfd, NULL, NULL);
        assert(fd >= 0);
        /* 헤더 끝을 보고, Content-Length만큼 본문까지 읽습니다. */
        b->req_len = 0;
        char *end = NULL;
        while (!end) {
            ssize_t n = read(fd, b->req + b->req_len, sizeof b->req - 1 - b->req_len);
            if (n <= 0)
                break;
            b->req_len += (size_t)n;
            b->req[b->req_len] = '\0';
            end = strstr(b->req, "\r\n\r\n");
        }
        if (end) {
            char *clh = strstr(b->req, "Content-Length: ");
            size_t want = (size_t)(end + 4 - b->req) + (clh ? strtoul(clh + 16, NULL, 10) : 0);
            while (b->req_len < want) {
                ssize_t n = read(fd, b->req + b->req_len, want - b->req_len);
                if (n <= 0)
                    break;
                b->req_len += (size_t)n;
            }
        }
        sleep_ms(b->delay_ms);
        if (b->resp_len)
            rio_writen(fd, (void *)b->resp, b->resp_len);
        sleep_ms(b->hold_ms);
        close(fd);
    }
    return NULL;
}

/* 127.0.0.1의 빈 포트에서 듣는 모의 백엔드를 띄우고 "/=127.0.0.1:<port>" 규칙을 spec에 씁니다. */
static void start_backend(backend *b, pthread_t *tid, char *hostport, size_t size)
{
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof a;
    b->listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(b->listenfd >= 0);
    assert(bind(b->listenfd, (struct sockaddr *)&a, sizeof a) == 0);
    assert(listen(b->listenfd, 8) == 0);
    assert(getsockname(b->listenfd, (struct sockaddr *)&a, &len) == 0);
    snprintf(hostport, size, "127.0.0.1:%d", ntohs(a.sin_port));
    assert(pthread_create(tid, NULL, backend_main, b) == 0);
}

static void stop_backend(backend *b, pthread_t tid)
{
    pthread_join(tid, NULL);
    close(b->listenfd);
}

typedef struct {
    int fd;
    proxy_router *r;
    http_request *req;
    const char *raw;
    int rc;
} serve_args;

static void *serve_main(void *arg)
{
    serve_args *s = arg;
    s->rc = proxy_serve(s->fd, s->r, &s->r->routes[0], s->req, s->raw, strlen(s->raw));
    close(s->fd);
    return NULL;
}

/* raw 요청을 proxy_serve()로 보내고 클라이언트가 받은 응답 전체를 resp에 담습니다. 받은 바이트 수를 반환합니다. */
static size_t run(proxy_router *r, const char *method, const char *uri, const char *body, size_t body_len,
                  char *resp, size_t size, int *rc)
{
    char raw[256];
    http_request req;
    make_req(&req, method, uri, body_len);
    req.body = (char *)body;
    snprintf(raw, sizeof raw, "%s %s HTTP/1.1\r\nHost: front\r\nContent-Length: %zu\r\n\r\n", method, uri, body_len);

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    serve_args s = {.fd = sv[0], .r = r, .req = &req, .raw = raw};
    pthread_t tid;
    assert(pthread_create(&tid, NULL, serve_main, &s) == 0);
    size_t len = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + len, size - 1 - len)) > 0)
        len += (size_t)n;
    resp[len] = '\0';
    close(sv[1]);
    pthread_join(tid, NULL);
    if (rc)
        *rc = s.rc;
    return len;
}

static void test_loopback(void)
{
    static char resp[200000];
    char hp1[64], hp2[64], spec[160];
    pthread_t t1, t2;
    int rc;

    /* P11: 백엔드 2대에 교대로 GET 전달, 이진 POST 본문 전달 */
    static const char r1[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nKeep-Alive: x\r\n\r\nb1";
    static const char r2[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nb2";
    backend b1 = {.resp = r1, .resp_len = sizeof r1 - 1, .nconns = 2};
    backend b2 = {.resp = r2, .resp_len = sizeof r2 - 1, .nconns = 2};
    start_backend(&b1, &t1, hp1, sizeof hp1);
    start_backend(&b2, &t2, hp2, sizeof hp2);
    proxy_router r;
    proxy_router_init(&r, 2000);
    snprintf(spec, sizeof spec, "/=%s,%s", hp1, hp2);
    assert(add(&r, spec) == 0);

    run(&r, "GET", "/a?x=1", NULL, 0, resp, sizeof resp, &rc);
    assert(rc == 0);
    assert(strcmp(resp, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nb1") == 0);
    assert(strncmp(b1.req, "GET /a?x=1 HTTP/1.0\r\n", 21) == 0);
    assert(strstr(b1.req, "Connection: close\r\n") && !strstr(b1.req, "Host: front"));
    run(&r, "GET", "/b", NULL, 0, resp, sizeof resp, &rc);
    assert(strstr(resp, "\r\n\r\nb2") != NULL);
    run(&r, "GET", "/c", NULL, 0, resp, sizeof resp, &rc);
    assert(strstr(resp, "\r\n\r\nb1") != NULL);
    const char bin[] = {'a', '\0', 'b', '\r', '\n'};
    run(&r, "POST", "/p", bin, sizeof bin, resp, sizeof resp, &rc);
    assert(strstr(resp, "\r\n\r\nb2") != NULL);
    assert(strstr(b2.req, "Content-Length: 5\r\n") != NULL);
    assert(memcmp(b2.req + b2.req_len - sizeof bin, bin, sizeof bin) == 0);
    stop_backend(&b1, t1);
    stop_backend(&b2, t2);
    proxy_router_destroy(&r);

    /* P12: 길이 없는 100 KiB 응답은 EOF까지 그대로 */
    static char big[120000];
    int hn = snprintf(big, sizeof big, "HTTP/1.0 200 OK\r\nContent-Type: a/b\r\n\r\n");
    for (int i = 0; i < 100000; i++)
        big[hn + i] = (char)('a' + i % 26);
    backend b3 = {.resp = big, .resp_len = (size_t)hn + 100000, .nconns = 1};
    start_backend(&b3, &t1, hp1, sizeof hp1);
    proxy_router_init(&r, 2000);
    snprintf(spec, sizeof spec, "/=%s", hp1);
    assert(add(&r, spec) == 0);
    size_t got = run(&r, "GET", "/big", NULL, 0, resp, sizeof resp, &rc);
    char *body = strstr(resp, "\r\n\r\n") + 4;
    assert(rc == 0 && got - (size_t)(body - resp) == 100000);
    assert(memcmp(body, big + hn, 100000) == 0);
    stop_backend(&b3, t1);
    proxy_router_destroy(&r);
}

static void expect_status(const char *hostport, const char *backend_resp, int delay_ms, int hold_ms, int nconns,
                          const char *want, long max_ms)
{
    static char resp[65536];
    char hp[64], spec[160];
    pthread_t t;
    backend b = {.resp = backend_resp, .resp_len = backend_resp ? strlen(backend_resp) : 0,
                 .delay_ms = delay_ms, .hold_ms = hold_ms, .nconns = nconns};
    if (!hostport) {
        start_backend(&b, &t, hp, sizeof hp);
        hostport = hp;
    }
    proxy_router r;
    proxy_router_init(&r, 200);
    snprintf(spec, sizeof spec, "/=%s", hostport);
    assert(add(&r, spec) == 0);
    long start = now_ms();
    run(&r, "GET", "/", NULL, 0, resp, sizeof resp, NULL);
    long took = now_ms() - start;
    if (strncmp(resp, want, strlen(want)) != 0 || took > max_ms) {
        fprintf(stderr, "want '%s' within %ldms, got %ldms:\n%s\n", want, max_ms, took, resp);
        assert(0);
    }
    if (hostport == hp)
        stop_backend(&b, t);
    proxy_router_destroy(&r);
}

static void test_failures(void)
{
    /* P13: 닫힌 포트는 곧바로 502 */
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof a;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(bind(fd, (struct sockaddr *)&a, sizeof a) == 0);
    assert(getsockname(fd, (struct sockaddr *)&a, &len) == 0);
    char closed[64];
    snprintf(closed, sizeof closed, "127.0.0.1:%d", ntohs(a.sin_port));
    close(fd);
    expect_status(closed, NULL, 0, 0, 0, "HTTP/1.1 502 Bad gateway\r\n", 150);

    /* P14: 응답 없이 끊김, 깨진 상태 줄, Transfer-Encoding은 502 */
    expect_status(NULL, NULL, 0, 0, 1, "HTTP/1.1 502 ", 150);
    expect_status(NULL, "nonsense\r\n\r\n", 0, 0, 1, "HTTP/1.1 502 ", 150);
    expect_status(NULL, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 0, 0, 1, "HTTP/1.1 502 ",
                  150);

    /* P15: 200ms 안에 응답하지 않으면 504 */
    expect_status(NULL, "HTTP/1.1 200 OK\r\n\r\n", 500, 0, 1, "HTTP/1.1 504 Gateway timeout\r\n", 450);

    /* P16: Content-Length만큼 보내고 연결을 붙잡는 백엔드도 기다리지 않고 끝남 */
    expect_status(NULL, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 0, 1000, 1, "HTTP/1.1 200 OK\r\n", 150);

    /* P17: 헤더를 보낸 뒤 백엔드가 끊기면 502를 덧붙이지 않고 연결만 닫음 */
    static char resp[4096];
    char hp[64], spec[160];
    pthread_t t;
    static const char partial[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc";
    backend b = {.resp = partial, .resp_len = sizeof partial - 1, .nconns = 1};
    start_backend(&b, &t, hp, sizeof hp);
    proxy_router r;
    proxy_router_init(&r, 200);
    snprintf(spec, sizeof spec, "/=%s", hp);
    assert(add(&r, spec) == 0);
    int rc;
    run(&r, "GET", "/", NULL, 0, resp, sizeof resp, &rc);
    assert(rc == -1);
    assert(strcmp(resp, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabc") == 0);
    stop_backend(&b, t);
    proxy_router_destroy(&r);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    test_spec();
    test_match();
    test_round_robin();
    test_request_head();
    test_response_head();
    test_loopback();
    test_failures();
    printf("test_proxy: ok\n");
    return 0;
}
