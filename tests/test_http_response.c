/* test_http_response.c - http_response.c 단위 테스트 (docs/http-response/test-cases.md) */
#undef NDEBUG
#include <assert.h>

#include "http_response.h"

static void test_format(void)
{
    char buf[MAXLINE];
    const char *want = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 13\r\n"
                       "Connection: close\r\n\r\n";

    /* R01 */
    int n = http_format_head(buf, sizeof buf, 200, "text/html", 13);
    assert(n == (int)strlen(buf));
    assert(strcmp(buf, want) == 0);

    assert(http_format_head(buf, sizeof buf, 302, "text/html", 0) == -1); /* R02 */
    assert(http_format_head(buf, 10, 200, "text/html", 13) == -1);        /* R03a */
    assert(http_format_head(buf, strlen(want) + 1, 200, "text/html", 13) == (int)strlen(want)); /* R03b */
    assert(http_format_head(buf, strlen(want), 200, "text/html", 13) == -1);                  /* R03c */
}

static void test_send_error(void)
{
    /* R04 */
    const char *want = "HTTP/1.1 404 Not found\r\nContent-Type: text/html\r\nContent-Length: 49\r\n"
                       "Connection: close\r\n\r\n"
                       "<html><body><h1>404 Not found</h1></body></html>\n";
    int fds[2];
    assert(pipe(fds) == 0);
    assert(http_send_error(fds[1], 404) == 0);
    close(fds[1]);

    char got[MAXLINE];
    size_t total = 0;
    ssize_t r;
    while ((r = read(fds[0], got + total, sizeof got - 1 - total)) > 0)
        total += (size_t)r;
    assert(r == 0);
    close(fds[0]);
    got[total] = '\0';
    assert(total == strlen(want));
    assert(strcmp(got, want) == 0);

    /* R05 */
    assert(pipe(fds) == 0);
    assert(http_send_error(fds[1], 302) == -1);
    close(fds[1]);
    assert(read(fds[0], got, sizeof got) == 0);
    close(fds[0]);
}

int main(void)
{
    test_format();
    test_send_error();
    return 0;
}
