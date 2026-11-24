/* test_http_request.c - http_request.c 단위 테스트 (docs/http-request-parsing/test-cases.md) */
#undef NDEBUG
#include <assert.h>

#include "http_request.h"

static http_request req;

static int parse(const char *s)
{
    return http_parse_head(s, strlen(s), &req);
}

/* data를 파이프에 써 두고 읽기 끝을 rio로 감싸 http_read_head를 호출합니다. */
static int read_head(const char *data, size_t n, char *buf, size_t size, size_t *len, rio_t *rio)
{
    int fds[2];
    assert(pipe(fds) == 0);
    assert(write(fds[1], data, n) == (ssize_t)n);
    close(fds[1]);
    rio_readinitb(rio, fds[0]);
    return http_read_head(rio, buf, size, len);
}

static void test_parse(void)
{
    /* Q01 */
    assert(parse("GET /index.html HTTP/1.1\r\nHost: ex.com\r\n\r\n") == 0);
    assert(strcmp(req.method, "GET") == 0);
    assert(strcmp(req.uri, "/index.html") == 0);
    assert(strcmp(req.version, "HTTP/1.1") == 0);
    assert(strcmp(req.host, "ex.com") == 0);
    assert(req.content_length == 0);

    /* Q02 */
    assert(parse("POST /a HTTP/1.0\r\ncontent-length:  9\r\nCONNECTION: close\r\n\r\n") == 0);
    assert(req.content_length == 9);
    assert(strcmp(req.connection, "close") == 0);
    assert(req.host[0] == '\0');

    assert(parse("GET /\r\n\r\n") == 400);                     /* Q03 */
    assert(parse("GET index.html HTTP/1.1\r\n\r\n") == 400);   /* Q04 */
    assert(parse("GET / HTTP/2.0\r\n\r\n") == 505);            /* Q05 */

    /* Q06 */
    assert(parse("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n") == 400);
    assert(parse("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n") == 400);
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 1048577\r\n\r\n") == 400);
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 1048576\r\n\r\n") == 0);

    /* Q07 */
    assert(parse("GET / HTTP/1.1\r\nNoColonHere\r\n\r\n") == 400);
    assert(parse("GET  / HTTP/1.1\r\n\r\n") == 400);
    assert(parse("G(T / HTTP/1.1\r\n\r\n") == 400);

    /* Q13: 형식이 맞는 모르는 메서드는 파싱 성공(서버가 501) */
    assert(parse("get / HTTP/1.1\r\n\r\n") == 0);
    assert(strcmp(req.method, "get") == 0);
    assert(parse("M-SEARCH / HTTP/1.1\r\n\r\n") == 0);

    /* Q14 */
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n") == 400);
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 6\r\n\r\n") == 400);
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n") == 0);
    assert(req.content_length == 5);
    assert(parse("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") == 501);

    /* 추가 경계 */
    assert(parse("GET / HTTP/1.1 x\r\n\r\n") == 400);
    assert(parse("GET / FTP/1.0\r\n\r\n") == 400);
    assert(parse("GET / HTTP/1.1\r\nConnection: 0123456789012345678901234567890123\r\n\r\n") == 400);
    assert(parse("GET / HTTP/1.1\r\n") == 400); /* 빈 줄 없음 */
    assert(http_parse_head("GET /a\0b HTTP/1.1\r\n\r\n", 22, &req) == 400);
}

static void test_read(void)
{
    char buf[MAXLINE];
    size_t len;
    rio_t rio;
    const char ok[] = "POST /a HTTP/1.1\r\nContent-Length: 9\r\n\r\nx=15&y=21";

    /* 빈 줄 뒤의 본문은 rio 버퍼에 남아 http_read_body가 꺼냅니다. */
    assert(read_head(ok, sizeof ok - 1, buf, sizeof buf, &len, &rio) == HTTP_READ_OK);
    assert(len == sizeof ok - 1 - 9);
    http_request r = {0};
    assert(http_parse_head(buf, len, &r) == 0);
    assert(http_read_body(&rio, &r) == HTTP_READ_OK);
    assert(memcmp(r.body, "x=15&y=21", 9) == 0);
    http_request_free(&r);
    assert(r.body == NULL);
    close(rio.rio_fd);

    /* Q12: 본문이 짧음 */
    const char shortb[] = "POST /a HTTP/1.1\r\nContent-Length: 9\r\n\r\nx=1";
    assert(read_head(shortb, sizeof shortb - 1, buf, sizeof buf, &len, &rio) == HTTP_READ_OK);
    assert(http_parse_head(buf, len, &r) == 0);
    assert(http_read_body(&rio, &r) == HTTP_READ_CLOSED);
    http_request_free(&r);
    close(rio.rio_fd);

    /* Q11: 빈 줄 전 EOF, 줄 중간 EOF */
    const char noend[] = "GET / HTTP/1.1\r\nHost: x\r\n";
    assert(read_head(noend, sizeof noend - 1, buf, sizeof buf, &len, &rio) == HTTP_READ_CLOSED);
    close(rio.rio_fd);
    const char mid[] = "GET / HTTP/1.1\r\nHo";
    assert(read_head(mid, sizeof mid - 1, buf, sizeof buf, &len, &rio) == HTTP_READ_CLOSED);
    close(rio.rio_fd);

    /* CR 없는 줄 끝 */
    const char lf[] = "GET / HTTP/1.1\n\n";
    assert(read_head(lf, sizeof lf - 1, buf, sizeof buf, &len, &rio) == HTTP_READ_BAD);
    close(rio.rio_fd);

    /* Q10: 헤더 블록이 버퍼보다 큼 */
    static char big[9100];
    memset(big, 'a', sizeof big);
    memcpy(big, "GET /", 5);
    memcpy(big + sizeof big - 13, " HTTP/1.1\r\n\r\n", 13);
    assert(read_head(big, sizeof big, buf, sizeof buf, &len, &rio) == HTTP_READ_BAD);
    close(rio.rio_fd);

    /* 줄마다 딱 맞아 남은 공간이 3바이트 미만이 되는 경우: EOF로 오인하지 않음 */
    char small[8];
    const char two[] = "ABCDE\r\nX\r\n\r\n";
    assert(read_head(two, sizeof two - 1, small, sizeof small, &len, &rio) == HTTP_READ_BAD);
    close(rio.rio_fd);
}

int main(void)
{
    test_parse();
    test_read();
    puts("test_http_request: OK");
    return 0;
}
