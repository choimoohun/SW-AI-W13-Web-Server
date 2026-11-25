/*
 * proxy.c - 경로 접두사 라우팅과 라운드로빈 리버스 프록시
 * 설계: docs/reverse-proxy/
 */
#include "proxy.h"

#include <ctype.h>
#include <poll.h>
#include <stdint.h>
#include <strings.h>
#include <time.h>

#include "http_response.h"

static void set_err(char *errbuf, size_t errsize, const char *fmt, const char *arg)
{
    snprintf(errbuf, errsize, fmt, arg);
}

void proxy_router_init(proxy_router *router, int timeout_ms)
{
    router->nroutes = 0;
    router->timeout_ms = timeout_ms > 0 ? timeout_ms : PROXY_DEFAULT_TIMEOUT_MS;
}

void proxy_router_destroy(proxy_router *router)
{
    for (size_t i = 0; i < router->nroutes; i++)
        pthread_mutex_destroy(&router->routes[i].lock);
    router->nroutes = 0;
}

/* "host:port" 또는 "[v6]:port" 하나(n바이트)를 해석해 b에 채웁니다. */
static int parse_backend(const char *s, size_t n, proxy_backend *b, char *errbuf, size_t errsize)
{
    char tmp[PROXY_HOST_MAX + PROXY_PORT_MAX];
    const char *host, *port;
    size_t hlen;

    if (n == 0 || n >= sizeof tmp) {
        set_err(errbuf, errsize, "empty or too long backend%s", "");
        return -1;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    if (tmp[0] == '[') {
        char *close = strchr(tmp, ']');
        if (!close || close[1] != ':')
            goto bad;
        host = tmp + 1;
        hlen = (size_t)(close - host);
        port = close + 2;
    } else {
        /* 괄호 없는 IPv6("::1")는 마지막 ':'를 포트로 잘못 읽으므로 ':'가 둘 이상이면 거부합니다. */
        char *colon = strrchr(tmp, ':');
        if (!colon || strchr(tmp, ':') != colon)
            goto bad;
        host = tmp;
        hlen = (size_t)(colon - tmp);
        port = colon + 1;
    }
    size_t plen = strlen(port);
    long pnum = 0;
    if (hlen == 0 || hlen >= sizeof b->host || plen == 0 || plen > 5)
        goto bad;
    for (size_t i = 0; i < plen; i++) {
        if (!isdigit((unsigned char)port[i]))
            goto bad;
        pnum = pnum * 10 + (port[i] - '0');
    }
    if (pnum < 1 || pnum > 65535)
        goto bad;
    memcpy(b->host, host, hlen);
    b->host[hlen] = '\0';
    snprintf(b->port, sizeof b->port, "%s", port);

    /* 요청마다 해석하면 시간 제한 없이 막힐 수 있어 시작할 때 한 번만 해석합니다. */
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = AI_NUMERICSERV};
    struct addrinfo *res;
    int rc = getaddrinfo(b->host, b->port, &hints, &res);
    if (rc != 0) {
        snprintf(errbuf, errsize, "cannot resolve '%s': %s", tmp, gai_strerror(rc));
        return -1;
    }
    /* IPv6가 꺼진 Docker에서 "localhost"가 ::1로 먼저 풀리면 IPv4로만 듣는 httpd 백엔드에 닿지 못하므로 IPv4를 먼저 고릅니다.
       shortcut: 주소 하나만 써서 IPv6로만 듣는 백엔드를 호스트 이름으로 적으면 502. 그때는 [::1]처럼 주소로 적음 */
    struct addrinfo *ai = res;
    for (struct addrinfo *p = res; p; p = p->ai_next)
        if (p->ai_family == AF_INET) {
            ai = p;
            break;
        }
    memcpy(&b->addr, ai->ai_addr, ai->ai_addrlen);
    b->addrlen = ai->ai_addrlen;
    b->family = ai->ai_family;
    b->socktype = ai->ai_socktype;
    b->protocol = ai->ai_protocol;
    freeaddrinfo(res);
    return 0;
bad:
    set_err(errbuf, errsize, "bad backend '%s' (want host:port, port 1..65535)", tmp);
    return -1;
}

int proxy_router_add_spec(proxy_router *router, const char *spec, char *errbuf, size_t errsize)
{
    if (router->nroutes == PROXY_MAX_ROUTES) {
        set_err(errbuf, errsize, "too many routes (max %s)", "16");
        return -1;
    }
    const char *eq = strchr(spec, '=');
    if (spec[0] != '/' || !eq) {
        set_err(errbuf, errsize, "bad route '%s' (want /prefix=host:port[,host:port...])", spec);
        return -1;
    }
    size_t plen = (size_t)(eq - spec);
    for (size_t i = 0; i < plen; i++)
        if ((unsigned char)spec[i] <= ' ' || spec[i] == 127) {
            set_err(errbuf, errsize, "bad route '%s' (space in prefix)", spec);
            return -1;
        }
    while (plen > 1 && spec[plen - 1] == '/')
        plen--;
    if (plen >= PROXY_PREFIX_MAX) {
        set_err(errbuf, errsize, "prefix too long in '%s'", spec);
        return -1;
    }

    /* 성공할 때까지 nroutes를 올리지 않아 실패해도 라우터가 바뀌지 않습니다. */
    proxy_route *rt = &router->routes[router->nroutes];
    memset(rt, 0, sizeof *rt);
    memcpy(rt->prefix, spec, plen);
    rt->prefix_len = plen;
    for (size_t i = 0; i < router->nroutes; i++)
        if (strcmp(router->routes[i].prefix, rt->prefix) == 0) {
            set_err(errbuf, errsize, "duplicate prefix '%s'", rt->prefix);
            return -1;
        }

    for (const char *p = eq + 1;;) {
        size_t n = strcspn(p, ",");
        if (rt->nbackends == PROXY_MAX_BACKENDS) {
            set_err(errbuf, errsize, "too many backends in '%s' (max 16)", spec);
            return -1;
        }
        if (parse_backend(p, n, &rt->backends[rt->nbackends], errbuf, errsize) < 0)
            return -1;
        rt->nbackends++;
        if (p[n] == '\0')
            break;
        p += n + 1;
    }
    if (pthread_mutex_init(&rt->lock, NULL) != 0) {
        set_err(errbuf, errsize, "pthread_mutex_init failed for '%s'", spec);
        return -1;
    }
    router->nroutes++;
    return 0;
}

proxy_route *proxy_router_match(proxy_router *router, const char *uri)
{
    proxy_route *best = NULL;
    for (size_t i = 0; i < router->nroutes; i++) {
        proxy_route *rt = &router->routes[i];
        size_t n = rt->prefix_len;
        int hit = (n == 1 && rt->prefix[0] == '/') ||
                  (strncmp(uri, rt->prefix, n) == 0 && (uri[n] == '\0' || uri[n] == '/' || uri[n] == '?'));
        if (hit && (!best || n > best->prefix_len))
            best = rt;
    }
    return best;
}

const proxy_backend *proxy_route_next(proxy_route *route)
{
    pthread_mutex_lock(&route->lock);
    const proxy_backend *b = &route->backends[route->next];
    route->next = (route->next + 1) % route->nbackends;
    pthread_mutex_unlock(&route->lock);
    return b;
}

/* ---- 헤더 필터 ---- */

static int name_is(const char *name, size_t n, const char *want)
{
    return n == strlen(want) && strncasecmp(name, want, n) == 0;
}

static int is_tchar(char c)
{
    return isalnum((unsigned char)c) || (c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

/* 헤더 블록 [p, end)의 어느 Connection: 값에 name이 토큰으로 적혀 있으면 1입니다(RFC 9110 7.6.1). */
static int listed_in_connection(const char *p, const char *end, const char *name, size_t n)
{
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl)
            break;
        const char *colon = memchr(p, ':', (size_t)(nl - p));
        if (colon && name_is(p, (size_t)(colon - p), "Connection")) {
            for (const char *t = colon + 1; t < nl;) {
                while (t < nl && strchr(" \t,\r", *t))
                    t++;
                const char *s = t;
                while (t < nl && !strchr(" \t,\r", *t))
                    t++;
                if ((size_t)(t - s) == n && strncasecmp(s, name, n) == 0)
                    return 1;
            }
        }
        p = nl + 1;
    }
    return 0;
}

static int is_hop_by_hop(const char *name, size_t n, int request)
{
    static const char *const hop[] = {"Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
                                      "Proxy-Connection", "TE", "Trailer", "Transfer-Encoding", "Upgrade"};
    for (size_t i = 0; i < sizeof hop / sizeof hop[0]; i++)
        if (name_is(name, n, hop[i]))
            return 1;
    return request && name_is(name, n, "Expect"); /* 백엔드가 100 Continue를 보내지 않게 합니다. */
}

static int append(char *out, size_t size, size_t *pos, const char *s, size_t n)
{
    if (*pos + n >= size)
        return -1;
    memcpy(out + *pos, s, n);
    *pos += n;
    out[*pos] = '\0';
    return 0;
}

/*
 * head(첫 줄 포함, 빈 줄로 끝남)의 헤더 줄을 검사해 남길 줄만 out에 덧붙입니다.
 * request면 Host와 Expect도 빼고, 아니면 Transfer-Encoding을 오류로 보고 Content-Length를 has_cl, cl에 모읍니다.
 * 두 방향 모두 Content-Length 줄은 빼고 호출자가 하나로 다시 씁니다.
 */
static int filter_headers(const char *head, size_t len, int request, char *out, size_t size, size_t *pos,
                          int *has_cl, size_t *cl)
{
    const char *end = head + len;
    const char *p = memchr(head, '\n', len);
    if (!p || memchr(head, '\0', len))
        return -1;
    for (p++; p < end;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl || nl == p || nl[-1] != '\r')
            return -1;
        size_t n = (size_t)(nl - 1 - p);
        if (n == 0)
            return 0; /* 빈 줄 */
        /* 단독 '\r' 같은 제어 문자는 받는 쪽에 따라 줄 끝으로 읽혀 헤더를 몰래 끼워 넣을 수 있습니다(RFC 9110 5.5). */
        for (size_t i = 0; i < n; i++)
            if (((unsigned char)p[i] < 0x20 && p[i] != '\t') || p[i] == 0x7f)
                return -1;
        const char *colon = memchr(p, ':', n);
        if (!colon || colon == p)
            return -1;
        size_t namelen = (size_t)(colon - p);
        /* "Transfer-Encoding : chunked"처럼 이름 뒤 공백으로 파서를 피한 줄을 그대로 전달하지 않습니다. */
        for (size_t i = 0; i < namelen; i++)
            if (!is_tchar(p[i]))
                return -1;

        if (name_is(p, namelen, "Content-Length")) {
            if (!request) {
                const char *v = colon + 1, *vend = p + n;
                size_t val = 0;
                while (v < vend && (*v == ' ' || *v == '\t'))
                    v++;
                while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t'))
                    vend--;
                if (v == vend)
                    return -1;
                for (; v < vend; v++) {
                    if (*v < '0' || *v > '9' || val > (SIZE_MAX - 9) / 10)
                        return -1;
                    val = val * 10 + (size_t)(*v - '0');
                }
                if (*has_cl && *cl != val)
                    return -1;
                *has_cl = 1;
                *cl = val;
            }
        } else if (!request && name_is(p, namelen, "Transfer-Encoding")) {
            return -1; /* 청크 디코더가 없어 본문 경계를 알 수 없습니다. */
        } else if (!(request && name_is(p, namelen, "Host")) && !is_hop_by_hop(p, namelen, request) &&
                   !listed_in_connection(head, end, p, namelen)) {
            if (append(out, size, pos, p, n + 2) < 0)
                return -1;
        }
        p = nl + 1;
    }
    return -1; /* 빈 줄이 없음 */
}

int proxy_format_request_head(const http_request *req, const char *raw_head, size_t raw_len,
                              const proxy_backend *backend, char *out, size_t out_size)
{
    /* 요청 줄은 다시 만들므로 URI 안의 단독 '\r' 같은 제어 문자도 여기서 막아야 헤더를 끼워 넣지 못합니다. */
    for (const char *c = req->uri; *c; c++)
        if ((unsigned char)*c < 0x20 || *c == 0x7f)
            return -1;
    int n = snprintf(out, out_size, "%s %s HTTP/1.0\r\n", req->method, req->uri);
    if (n < 0 || (size_t)n >= out_size)
        return -1;
    size_t pos = (size_t)n;
    if (filter_headers(raw_head, raw_len, 1, out, out_size, &pos, NULL, NULL) < 0)
        return -1;
    int v6 = strchr(backend->host, ':') != NULL;
    n = snprintf(out + pos, out_size - pos, "Host: %s%s%s:%s\r\nConnection: close\r\n", v6 ? "[" : "",
                 backend->host, v6 ? "]" : "", backend->port);
    if (n < 0 || (size_t)n >= out_size - pos)
        return -1;
    pos += (size_t)n;
    if (req->content_length > 0 || strcmp(req->method, "POST") == 0) {
        n = snprintf(out + pos, out_size - pos, "Content-Length: %zu\r\n", req->content_length);
        if (n < 0 || (size_t)n >= out_size - pos)
            return -1;
        pos += (size_t)n;
    }
    if (append(out, out_size, &pos, "\r\n", 2) < 0)
        return -1;
    return (int)pos;
}

int proxy_filter_response_head(const char *raw_head, size_t raw_len, char *out, size_t out_size, int *status_out,
                               int *has_cl, size_t *cl_out)
{
    const char *nl = memchr(raw_head, '\n', raw_len);
    if (!nl || nl == raw_head || nl[-1] != '\r')
        return -1;
    size_t n = (size_t)(nl + 1 - raw_head); /* CRLF 포함 */
    /* "HTTP/1.x DDD" 뒤에는 줄 끝이나 공백+사유 문구가 옵니다. */
    if (n < 14 || (strncmp(raw_head, "HTTP/1.0 ", 9) != 0 && strncmp(raw_head, "HTTP/1.1 ", 9) != 0) ||
        !isdigit((unsigned char)raw_head[9]) || !isdigit((unsigned char)raw_head[10]) ||
        !isdigit((unsigned char)raw_head[11]) || (raw_head[12] != ' ' && raw_head[12] != '\r') ||
        memchr(raw_head, '\r', n - 2)) /* 상태 줄 안의 단독 '\r'로 헤더를 끼워 넣지 못하게 함 */
        return -1;
    int status = (raw_head[9] - '0') * 100 + (raw_head[10] - '0') * 10 + (raw_head[11] - '0');
    if (status < 200 || status > 599)
        return -1;

    size_t pos = 0;
    *has_cl = 0;
    *cl_out = 0;
    if (append(out, out_size, &pos, raw_head, n) < 0 ||
        filter_headers(raw_head, raw_len, 0, out, out_size, &pos, has_cl, cl_out) < 0)
        return -1;
    if (*has_cl) {
        int m = snprintf(out + pos, out_size - pos, "Content-Length: %zu\r\n", *cl_out);
        if (m < 0 || (size_t)m >= out_size - pos)
            return -1;
        pos += (size_t)m;
    }
    if (append(out, out_size, &pos, "Connection: close\r\n\r\n", 21) < 0)
        return -1;
    *status_out = status;
    return (int)pos;
}

/* ---- 소켓 I/O ---- */

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int timed_out(int err)
{
    return err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT;
}

/*
 * b에 timeout_ms 안에 연결합니다. csapp의 open_clientfd()는 connect()에 시간 제한이 없고
 * FD_CLOEXEC를 붙이지 않아 쓰지 않습니다. 성공하면 블로킹 소켓, 실패하면 -1과 *status(502/504)입니다.
 */
static int proxy_connect(const proxy_backend *b, int timeout_ms, int *status)
{
    *status = 502;
    int fd = socket(b->family, b->socktype, b->protocol);
    if (fd < 0)
        return -1;
    /* shortcut: socket()과 fcntl() 사이에 다른 워커가 fork하면 CGI에 샘. accept()와 같은 틈이라 둠 */
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
        goto fail;

    if (connect(fd, (const struct sockaddr *)&b->addr, b->addrlen) < 0) {
        if (errno != EINPROGRESS)
            goto fail;
        long deadline = now_ms() + timeout_ms;
        struct pollfd pfd = {.fd = fd, .events = POLLOUT};
        int rc;
        do {
            long rem = deadline - now_ms();
            rc = poll(&pfd, 1, rem > 0 ? (int)rem : 0);
        } while (rc < 0 && errno == EINTR);
        if (rc == 0) {
            *status = 504;
            errno = ETIMEDOUT;
            goto fail;
        }
        int so_err = 0;
        socklen_t len = sizeof so_err;
        if (rc < 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0)
            goto fail;
        if (so_err != 0) {
            errno = so_err;
            if (so_err == ETIMEDOUT)
                *status = 504;
            goto fail;
        }
    }
    /* RIO는 블로킹 소켓을 가정하므로 되돌리고, 읽기·쓰기마다 시간 제한을 겁니다. */
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    if (fcntl(fd, F_SETFL, fl) < 0 || setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0)
        goto fail;
    return fd;
fail:;
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

static void log_error(const proxy_backend *b, const char *phase, int status, int err, size_t sent)
{
    char msg[128] = "unknown error";
    strerror_r(err, msg, sizeof msg); /* 워커 스레드에서 부르므로 strerror()의 공유 버퍼를 쓰지 않습니다. */
    fprintf(stderr, "proxy error: backend=%s:%s phase=%s status=%d errno=%d (%s) bytes_sent=%zu\n", b->host,
            b->port, phase, status, err, msg, sent);
}

/* 헤더를 보낸 뒤 본문을 클라이언트로 흘려 보냅니다. Content-Length가 있으면 그만큼만 보내고 EOF를 기다리지 않습니다.
   shortcut: 시간 제한이 read 한 번마다라 바이트를 흘려 보내는 백엔드는 워커를 오래 잡음. 백엔드를 믿지 못하면 전체 마감 시각을 둠 */
static int stream_body(int connfd, int upfd, rio_t *up, int has_cl, size_t cl, size_t *sent)
{
    char chunk[RIO_BUFSIZE];
    size_t remaining = cl;

    /* 헤더를 읽으며 RIO 버퍼에 미리 들어온 본문부터 보냅니다. */
    if (up->rio_cnt > 0) {
        size_t n = (size_t)up->rio_cnt;
        if (has_cl && n > remaining)
            n = remaining;
        if (rio_writen(connfd, up->rio_bufptr, n) != (ssize_t)n)
            return -1;
        *sent += n;
        remaining -= has_cl ? n : 0;
    }
    while (!has_cl || remaining > 0) {
        size_t want = has_cl && remaining < sizeof chunk ? remaining : sizeof chunk;
        ssize_t n = read(upfd, chunk, want);
        if (n < 0 && errno == EINTR)
            continue;
        if (n == 0 && !has_cl)
            return 0; /* 길이 없는 응답은 EOF가 끝입니다. */
        if (n <= 0) {
            if (n == 0)
                errno = ECONNRESET; /* Content-Length보다 먼저 끊김 */
            return -1;
        }
        if (rio_writen(connfd, chunk, (size_t)n) != n)
            return -1;
        *sent += (size_t)n;
        remaining -= has_cl ? (size_t)n : 0;
    }
    return 0;
}

int proxy_serve(int connfd, const proxy_router *router, proxy_route *route, const http_request *req,
                const char *raw_head, size_t raw_len)
{
    const proxy_backend *b = proxy_route_next(route);
    char buf[PROXY_HEAD_BUF], head[MAXLINE];
    size_t head_len, sent = 0;
    int status = 502, upfd = -1;
    const char *phase;
    rio_t up;

    int n = proxy_format_request_head(req, raw_head, raw_len, b, buf, sizeof buf);
    if (n < 0) {
        log_error(b, "format", 400, EINVAL, 0);
        return http_send_error(connfd, 400);
    }

    phase = "connect";
    if ((upfd = proxy_connect(b, router->timeout_ms, &status)) < 0)
        goto gateway_error;

    phase = "send";
    if (rio_writen(upfd, buf, (size_t)n) != n ||
        (req->content_length > 0 &&
         rio_writen(upfd, req->body, req->content_length) != (ssize_t)req->content_length)) {
        status = timed_out(errno) ? 504 : 502;
        goto gateway_error;
    }

    phase = "read_head";
    rio_readinitb(&up, upfd);
    errno = 0;
    int rc = http_read_head(&up, head, sizeof head, &head_len);
    if (rc != HTTP_READ_OK) {
        status = rc == HTTP_READ_CLOSED && timed_out(errno) ? 504 : 502;
        goto gateway_error;
    }
    int up_status, has_cl;
    size_t cl;
    errno = 0;
    n = proxy_filter_response_head(head, head_len, buf, sizeof buf, &up_status, &has_cl, &cl);
    if (n < 0) {
        status = 502;
        errno = EPROTO;
        goto gateway_error;
    }

    /* 여기부터는 상태 줄을 보냈으므로 실패해도 다른 상태 코드를 덧쓰지 않고 연결만 닫습니다. */
    if (rio_writen(connfd, buf, (size_t)n) != n) {
        log_error(b, "send_head", up_status, errno, 0);
        close(upfd);
        return -1;
    }
    rc = 0;
    if (up_status != 204 && up_status != 304 && stream_body(connfd, upfd, &up, has_cl, cl, &sent) < 0) {
        log_error(b, "stream", up_status, errno, sent);
        rc = -1;
    }
    close(upfd);
    return rc;

gateway_error:
    log_error(b, phase, status, errno, 0);
    if (upfd >= 0)
        close(upfd);
    return http_send_error(connfd, status);
}
