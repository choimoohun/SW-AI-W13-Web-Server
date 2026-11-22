/*
 * http_parser.c — 메모리 버퍼를 HTTP 요청 구조로 해석하는 순수 파서. 소켓 IO 없음.
 *
 * [핵심 원리 — 발표용]
 * - TCP는 메시지 경계가 없는 바이트 스트림 → read 경계 ≠ 요청 경계. 누적 버퍼 길이만큼만 검사.
 * - 요청 라인: METHOD SP URI SP VERSION CRLF. 헤더는 CRLF 단위, 끝은 빈 줄(\r\n\r\n).
 * - Host/Content-Length/Connection은 헤더 이름 대소문자 무시. 입력은 포인터+길이(NUL 가정 안 함).
 * - Content-Length는 '본문 바이트 수'. 이 값으로 본문 경계를 판단. 중복/모호한 값은 거절.
 * - 반환: COMPLETE(완성) / NEED_MORE(추가 입력) / ERROR(err_status).
 * 요구사항: FR-02.
 */

#include "http_parser.h"
#include <string.h>    /* memcmp, memchr, memcpy */
#include <strings.h>   /* strncasecmp (POSIX, 대소문자 무시 비교) */
#include <stdlib.h>    /* strtol */

#define MAX_URI     2048
#define MAX_HEADER  8192
#define MAX_BODY    1048576L   /* 1MiB */

/* 길이 제한 버퍼에서 부분문자열 탐색(NUL 가정 없이). 찾으면 위치, 없으면 NULL. */
static const char *find_sub(const char *hay, size_t haylen, const char *needle, size_t nlen)
{
    if (nlen == 0 || haylen < nlen) return NULL;
    for (size_t i = 0; i + nlen <= haylen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return NULL;
}

static parse_status_t fail(http_request_t *req, int status)
{
    req->err_status = status;
    return PARSE_ERROR;
}

parse_status_t http_parser_parse(const char *buf, size_t len, http_request_t *req)
{
    memset(req, 0, sizeof(*req));
    req->content_length = -1;   /* 아직 없음 */

    /* 1) 헤더 끝(\r\n\r\n) 탐색 */
    const char *hdr_end = find_sub(buf, len, "\r\n\r\n", 4);
    if (!hdr_end) {
        if (len > MAX_HEADER) return fail(req, 431);   /* 헤더가 너무 큼 */
        return PARSE_NEED_MORE;                        /* 아직 덜 도착 */
    }
    size_t header_len = (size_t)(hdr_end - buf) + 4;
    if (header_len > MAX_HEADER) return fail(req, 431);

    /* 2) 요청 라인(첫 줄) 분해: METHOD SP URI SP VERSION */
    const char *line_end = find_sub(buf, len, "\r\n", 2);
    size_t line_len = (size_t)(line_end - buf);

    const char *sp1 = memchr(buf, ' ', line_len);
    if (!sp1) return fail(req, 400);
    req->method = buf;
    req->method_len = (size_t)(sp1 - buf);

    const char *uri_start = sp1 + 1;
    const char *sp2 = memchr(uri_start, ' ', (size_t)(line_end - uri_start));
    if (!sp2) return fail(req, 400);
    req->uri = uri_start;
    req->uri_len = (size_t)(sp2 - uri_start);

    const char *ver = sp2 + 1;
    size_t ver_len = (size_t)(line_end - ver);
    if (memchr(ver, ' ', ver_len)) return fail(req, 400);   /* 토큰이 3개를 넘음 */
    if (ver_len == 8 && memcmp(ver, "HTTP/1.1", 8) == 0)      req->http_minor = 1;
    else if (ver_len == 8 && memcmp(ver, "HTTP/1.0", 8) == 0) req->http_minor = 0;
    else if (ver_len >= 5 && memcmp(ver, "HTTP/", 5) == 0)    return fail(req, 505);
    else                                                      return fail(req, 400);

    /* 메서드 / URI 검증 */
    int is_get  = (req->method_len == 3 && memcmp(req->method, "GET", 3) == 0);
    int is_post = (req->method_len == 4 && memcmp(req->method, "POST", 4) == 0);
    if (!is_get && !is_post) return fail(req, 405);
    if (req->uri_len == 0) return fail(req, 400);
    if (req->uri_len > MAX_URI) return fail(req, 414);

    /* 3) 헤더 순회(요청 라인 다음 ~ 마지막 헤더의 CRLF 까지) */
    const char *p = line_end + 2;
    const char *hend = hdr_end + 2;   /* 마지막 헤더를 끝내는 \r\n 포함 */
    while (p < hend) {
        const char *eol = find_sub(p, (size_t)(hend - p), "\r\n", 2);
        if (!eol) return fail(req, 400);
        size_t hlen = (size_t)(eol - p);
        const char *colon = memchr(p, ':', hlen);
        if (!colon) return fail(req, 400);
        size_t name_len = (size_t)(colon - p);
        const char *val = colon + 1;
        size_t val_len = (size_t)(eol - val);
        while (val_len && (*val == ' ' || *val == '\t')) { val++; val_len--; }
        while (val_len && (val[val_len-1] == ' ' || val[val_len-1] == '\t')) val_len--;

        if (name_len == 4 && strncasecmp(p, "Host", 4) == 0) {
            req->host = val; req->host_len = val_len;
        } else if (name_len == 14 && strncasecmp(p, "Content-Length", 14) == 0) {
            if (req->content_length != -1) return fail(req, 400);   /* 중복 거절 */
            char tmp[32];
            if (val_len == 0 || val_len >= sizeof(tmp)) return fail(req, 400);
            memcpy(tmp, val, val_len); tmp[val_len] = '\0';
            char *end;
            long cl = strtol(tmp, &end, 10);
            if (*end != '\0' || cl < 0) return fail(req, 400);
            if (cl > MAX_BODY) return fail(req, 413);
            req->content_length = cl;
        } else if (name_len == 10 && strncasecmp(p, "Connection", 10) == 0) {
            if (val_len == 5 && strncasecmp(val, "close", 5) == 0) req->connection_close = 1;
        } else if (name_len == 17 && strncasecmp(p, "Transfer-Encoding", 17) == 0) {
            return fail(req, 501);   /* chunked 등 미지원 */
        }
        p = eol + 2;
    }

    /* HTTP/1.1 은 Host 필수 */
    if (req->http_minor == 1 && req->host == NULL) return fail(req, 400);

    /* 4) 본문 경계 판단 */
    const char *body_start = hdr_end + 4;
    size_t body_avail = len - (size_t)(body_start - buf);
    if (req->content_length >= 0) {
        if (body_avail < (size_t)req->content_length) return PARSE_NEED_MORE;
        req->body = body_start;
        req->body_len = (size_t)req->content_length;
        req->consumed = (size_t)(body_start - buf) + req->body_len;
        return PARSE_COMPLETE;
    }
    /* Content-Length 없음: POST 면 411, GET 등은 본문 없이 완성 */
    if (is_post) return fail(req, 411);
    req->body = body_start;
    req->body_len = 0;
    req->consumed = (size_t)(body_start - buf);
    return PARSE_COMPLETE;
}
