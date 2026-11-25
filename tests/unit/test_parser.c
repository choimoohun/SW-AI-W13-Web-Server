/*
 * test_parser.c — http_parser_parse 단위 테스트 (순수 파서, IO 없음).
 * 요구사항 §7: HTTP 요청 라인/헤더 파서 단위 검증.
 * 빌드: cc $(CFLAGS) -o test_parser tests/unit/test_parser.c src/http_parser.c
 */
#include "http_parser.h"
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static parse_status_t ps(const char *s, http_request_t *r)
{
    return http_parser_parse(s, strlen(s), r);
}

int main(void)
{
    http_request_t r;

    CHECK(ps("GET / HTTP/1.1\r\nHost: x\r\n\r\n", &r) == PARSE_COMPLETE, "normal GET complete");
    CHECK(r.method_len == 3 && memcmp(r.method, "GET", 3) == 0, "method GET");
    CHECK(r.uri_len == 1 && r.uri[0] == '/', "uri /");
    CHECK(r.http_minor == 1, "http 1.1");
    CHECK(r.host != NULL, "host present");

    CHECK(ps("GET / HTTP/1.1\r\n\r\n", &r) == PARSE_ERROR && r.err_status == 400, "1.1 no Host -> 400");
    CHECK(ps("GET / HTTP/1.0\r\n\r\n", &r) == PARSE_COMPLETE, "1.0 no Host ok");
    CHECK(ps("GET / HTTP/1.1\r\nhOsT: x\r\n\r\n", &r) == PARSE_COMPLETE && r.host != NULL, "case-insensitive Host");

    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabc", &r) == PARSE_COMPLETE
          && r.body_len == 3, "POST body complete");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nab", &r) == PARSE_NEED_MORE, "POST short body -> need more");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\n\r\n", &r) == PARSE_ERROR && r.err_status == 411, "POST no CL -> 411");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 2000000\r\n\r\n", &r) == PARSE_ERROR
          && r.err_status == 413, "POST CL > 1MiB -> 413");

    CHECK(ps("DELETE / HTTP/1.1\r\nHost: x\r\n\r\n", &r) == PARSE_ERROR && r.err_status == 405, "DELETE -> 405");
    CHECK(ps("GET / HTTP/2.0\r\nHost: x\r\n\r\n", &r) == PARSE_ERROR && r.err_status == 505, "HTTP/2.0 -> 505");
    CHECK(ps("BAD\r\n\r\n", &r) == PARSE_ERROR && r.err_status == 400, "malformed line -> 400");
    CHECK(ps("GET / HTTP/1.1\r\nHost: x\r\n", &r) == PARSE_NEED_MORE, "no blank line -> need more");
    CHECK(ps("GET / HTTP/1.1\r\nHo", &r) == PARSE_NEED_MORE, "partial header -> need more");
    CHECK(ps("GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", &r) == PARSE_ERROR
          && r.err_status == 501, "chunked -> 501");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n", &r) == PARSE_ERROR
          && r.err_status == 400, "duplicate CL -> 400");

    /* Content-Length 값은 전부 숫자여야 한다. strtol 만으로는 "+3", "-1", "3\0junk" 를 통과시킨다. */
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: +3\r\n\r\nabc", &r) == PARSE_ERROR
          && r.err_status == 400, "CL '+3' -> 400");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n", &r) == PARSE_ERROR
          && r.err_status == 400, "CL '-1' -> 400");
    CHECK(ps("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3x\r\n\r\nabc", &r) == PARSE_ERROR
          && r.err_status == 400, "CL '3x' -> 400");
    {
        char buf[96];
        size_t n = 0;
        static const char pre[] = "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: ";
        memcpy(buf, pre, sizeof(pre) - 1); n = sizeof(pre) - 1;
        buf[n++] = '3'; buf[n++] = '\0'; buf[n++] = 'j'; buf[n++] = 'u';
        buf[n++] = 'n'; buf[n++] = 'k';
        memcpy(buf + n, "\r\n\r\n", 4); n += 4;
        CHECK(http_parser_parse(buf, n, &r) == PARSE_ERROR && r.err_status == 400,
              "CL with embedded NUL -> 400");
    }

    /* 긴 URI (> 2048) -> 414 */
    char longuri[3000];
    memcpy(longuri, "GET /", 5);
    memset(longuri + 5, 'a', 2049);
    memcpy(longuri + 5 + 2049, " HTTP/1.1\r\nHost: x\r\n\r\n", 23);
    CHECK(http_parser_parse(longuri, strlen(longuri), &r) == PARSE_ERROR && r.err_status == 414, "long URI -> 414");

    /* 헤더가 8192를 넘으면 431. 아직 끝나지 않은 덩어리도 길이만으로 431. */
    {
        static const char pre[] = "GET / HTTP/1.1\r\nX-Pad: ";
        size_t n = sizeof(pre) - 1;
        char big[9000 + sizeof(pre)];
        memcpy(big, pre, n);
        memset(big + n, 'p', 9000);
        memcpy(big + n + 9000, "\r\n\r\n", 4);
        CHECK(http_parser_parse(big, n + 9000 + 4, &r) == PARSE_ERROR && r.err_status == 431,
              "header > 8192 -> 431");
    }
    {
        char partial[100];
        memset(partial, 'a', sizeof(partial));
        CHECK(http_parser_parse(partial, sizeof(partial), &r) == PARSE_NEED_MORE,
              "no blank line yet -> NEED_MORE(431 only over 8192)");
    }

    if (failures) { printf("test_parser: %d FAILED\n", failures); return 1; }
    printf("test_parser: all passed\n");
    return 0;
}
