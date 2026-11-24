/*
 * http_request.c - HTTP 요청 읽기와 파싱 (CS:APP3e 11.5.3, 10.5 RIO)
 * 설계: docs/http-request-parsing/
 */
#include "http_request.h"

#include <strings.h>

int http_read_head(rio_t *rp, char *buf, size_t size, size_t *len)
{
    size_t total = 0;

    for (;;) {
        size_t rem = size - total;
        /* rio_readlineb는 maxlen이 1이면 0(EOF)을 반환하므로 상한을 먼저 확인합니다. */
        if (size < total + 3)
            return HTTP_READ_BAD;
        ssize_t n = rio_readlineb(rp, buf + total, rem);
        if (n <= 0)
            return HTTP_READ_CLOSED;
        char *line = buf + total;
        if (line[n - 1] != '\n')
            return (size_t)n == rem - 1 ? HTTP_READ_BAD : HTTP_READ_CLOSED;
        if (n < 2 || line[n - 2] != '\r')
            return HTTP_READ_BAD;
        total += (size_t)n;
        if (n == 2) {
            *len = total;
            return HTTP_READ_OK;
        }
    }
}

/* [s, s+n)를 NUL로 끝내 dst에 복사합니다. 들어가지 않으면 -1입니다. */
static int copy_field(char *dst, size_t dstsize, const char *s, size_t n)
{
    if (n >= dstsize)
        return -1;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return 0;
}

static int name_is(const char *name, size_t n, const char *want)
{
    return n == strlen(want) && strncasecmp(name, want, n) == 0;
}

static int parse_request_line(const char *line, size_t n, http_request *req)
{
    const char *end = line + n;
    const char *sp1 = memchr(line, ' ', n);
    if (!sp1)
        return 400;
    const char *uri = sp1 + 1;
    const char *sp2 = memchr(uri, ' ', (size_t)(end - uri));
    if (!sp2)
        return 400;
    const char *ver = sp2 + 1;
    size_t mlen = (size_t)(sp1 - line), ulen = (size_t)(sp2 - uri), vlen = (size_t)(end - ver);

    /* 빈 토큰(연속 공백)과 네 번째 토큰을 막습니다. */
    if (mlen == 0 || ulen == 0 || vlen == 0 || memchr(ver, ' ', vlen))
        return 400;
    for (size_t i = 0; i < mlen; i++)
        if (line[i] < 'A' || line[i] > 'Z')
            return 400;
    if (uri[0] != '/')
        return 400;
    if (copy_field(req->method, sizeof req->method, line, mlen) < 0 ||
        copy_field(req->uri, sizeof req->uri, uri, ulen) < 0 ||
        copy_field(req->version, sizeof req->version, ver, vlen) < 0)
        return 400;
    if (strcmp(req->version, "HTTP/1.0") == 0 || strcmp(req->version, "HTTP/1.1") == 0)
        return 0;
    return strncmp(req->version, "HTTP/", 5) == 0 ? 505 : 400;
}

static int parse_header_line(const char *line, size_t n, http_request *req)
{
    const char *colon = memchr(line, ':', n);
    if (!colon || colon == line)
        return 400;
    size_t namelen = (size_t)(colon - line);
    const char *v = colon + 1, *vend = line + n;
    while (v < vend && (*v == ' ' || *v == '\t'))
        v++;
    while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t'))
        vend--;
    size_t vlen = (size_t)(vend - v);

    if (name_is(line, namelen, "Host"))
        return copy_field(req->host, sizeof req->host, v, vlen) < 0 ? 400 : 0;
    if (name_is(line, namelen, "Connection"))
        return copy_field(req->connection, sizeof req->connection, v, vlen) < 0 ? 400 : 0;
    if (name_is(line, namelen, "Content-Length")) {
        size_t cl = 0;
        if (vlen == 0)
            return 400;
        for (size_t i = 0; i < vlen; i++) {
            if (v[i] < '0' || v[i] > '9')
                return 400;
            cl = cl * 10 + (size_t)(v[i] - '0');
            if (cl > HTTP_MAX_BODY) /* 자릿수가 많아도 넘치기 전에 멈춥니다. */
                return 400;
        }
        req->content_length = cl;
    }
    return 0;
}

int http_parse_head(const char *buf, size_t len, http_request *req)
{
    const char *p = buf, *end = buf + len;
    int first = 1;

    req->host[0] = '\0';
    req->connection[0] = '\0';
    req->content_length = 0;
    /* 값 안의 NUL은 문자열 필드를 잘라 먹으므로 형식 오류로 봅니다. */
    if (memchr(buf, '\0', len))
        return 400;

    for (;;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl || nl == p || nl[-1] != '\r')
            return 400;
        size_t n = (size_t)(nl - 1 - p); /* CRLF를 뺀 길이 */
        if (n == 0)
            return first ? 400 : 0; /* 빈 줄: 헤더 블록의 끝 */
        int rc = first ? parse_request_line(p, n, req) : parse_header_line(p, n, req);
        if (rc != 0)
            return rc;
        first = 0;
        p = nl + 1;
    }
}

int http_read_body(rio_t *rp, http_request *req)
{
    if (req->content_length == 0)
        return HTTP_READ_OK;
    req->body = malloc(req->content_length);
    if (!req->body)
        return HTTP_READ_NOMEM;
    ssize_t n = rio_readnb(rp, req->body, req->content_length);
    if (n < 0 || (size_t)n != req->content_length)
        return HTTP_READ_CLOSED;
    return HTTP_READ_OK;
}

void http_request_free(http_request *req)
{
    free(req->body);
    req->body = NULL;
}
