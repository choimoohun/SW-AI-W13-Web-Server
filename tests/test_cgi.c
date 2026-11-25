/* test_cgi.c - cgi.c 단위 테스트 (docs/dynamic-request/test-cases.md) */
#undef NDEBUG
#include <assert.h>
#include <time.h>

#include "cgi.h"

static void test_parse(void)
{
    const char *type;
    size_t head_len;

    /* D01 */
    char crlf[] = "Content-Type: text/plain\r\n\r\nhi";
    assert(cgi_parse_output(crlf, strlen(crlf), &type, &head_len) == 0);
    assert(strcmp(type, "text/plain") == 0);
    assert(strcmp(crlf + head_len, "hi") == 0);

    /* D02: 대소문자 무시, LF 줄 끝, 다른 헤더 */
    char lf[] = "X-A: 1\ncontent-type:text/html\n\n<b>";
    assert(cgi_parse_output(lf, strlen(lf), &type, &head_len) == 0);
    assert(strcmp(type, "text/html") == 0);
    assert(strcmp(lf + head_len, "<b>") == 0);

    /* D03 */
    char no_type[] = "X-A: 1\r\n\r\nbody";
    assert(cgi_parse_output(no_type, strlen(no_type), &type, &head_len) == -1);
    char no_blank[] = "Content-Type: text/plain\r\n";
    assert(cgi_parse_output(no_blank, strlen(no_blank), &type, &head_len) == -1);
    char blank_type[] = "Content-Type:  \r\n\r\nx"; /* 빈 값 */
    assert(cgi_parse_output(blank_type, strlen(blank_type), &type, &head_len) == -1);
    char cr_type[] = "Content-Type: text/plain\rX: y\r\n\r\nx"; /* 줄 중간의 '\r' */
    assert(cgi_parse_output(cr_type, strlen(cr_type), &type, &head_len) == -1);
    char empty[] = "";
    assert(cgi_parse_output(empty, 0, &type, &head_len) == -1);

    /* 값 길이 경계: CGI_MAX_CONTENT_TYPE - 1바이트는 받고, CGI_MAX_CONTENT_TYPE바이트는 거부 */
    char value[CGI_MAX_CONTENT_TYPE + 1], out[CGI_MAX_CONTENT_TYPE + 32];
    memset(value, 'a', CGI_MAX_CONTENT_TYPE);
    value[CGI_MAX_CONTENT_TYPE] = '\0';
    snprintf(out, sizeof out, "Content-Type: %s\r\n\r\nx", value + 1);
    assert(cgi_parse_output(out, strlen(out), &type, &head_len) == 0);
    assert(strlen(type) == CGI_MAX_CONTENT_TYPE - 1);
    snprintf(out, sizeof out, "Content-Type: %s\r\n\r\nx", value);
    assert(cgi_parse_output(out, strlen(out), &type, &head_len) == -1);
}

static char dir[] = "/tmp/test_cgiXXXXXX";

static void make_script(const char *name, const char *body, mode_t mode)
{
    char path[64];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "#!/bin/sh\n%s", body);
    fclose(f);
    assert(chmod(path, mode) == 0);
}

/* name을 method, uri, body로 실행하고 응답 전체를 resp에 담습니다. */
static void serve(const char *name, const char *method, const char *uri, const char *body, char *resp, size_t size)
{
    http_request req = {0};
    char path[64];
    int sv[2];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(req.method, sizeof req.method, "%s", method);
    snprintf(req.uri, sizeof req.uri, "%s", uri);
    req.body = (char *)body;
    req.content_length = body ? strlen(body) : 0;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    cgi_serve(sv[0], path, &req);
    close(sv[0]);
    size_t len = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + len, size - 1 - len)) > 0)
        len += (size_t)n;
    resp[len] = '\0';
    close(sv[1]);
}

static void test_serve(void)
{
    char resp[1024];
    assert(mkdtemp(dir) != NULL);
    make_script("echo", "printf 'Content-Type: text/plain\\r\\n\\r\\n'\n"
                        "printf '%s|%s|%s|' \"$REQUEST_METHOD\" \"$QUERY_STRING\" \"$CONTENT_LENGTH\"\n"
                        "cat\n",
                0755);
    make_script("fail", "printf 'Content-Type: text/plain\\r\\n\\r\\nx'\nexit 1\n", 0755);
    make_script("noexec", "exit 0\n", 0644);
    make_script("hang", "sleep 30\n", 0755);
    make_script("linger", "printf 'Content-Type: text/plain\\r\\n\\r\\nx'\nexec >&-\nsleep 30\n", 0755);
    make_script("big", "printf 'Content-Type: text/plain\\r\\n\\r\\n'\nhead -c 2000000 /dev/zero\n", 0755);

    /* D04: 환경 변수와 본문이 CGI에 전달되고 출력이 200 본문이 됨 */
    serve("echo", "POST", "/cgi-bin/echo?x=1&y=2", "abc", resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 200 OK\r\n", 17) == 0);
    assert(strstr(resp, "Content-Type: text/plain\r\n") != NULL);
    assert(strstr(resp, "Content-Length: 18\r\n") != NULL);
    assert(strcmp(strstr(resp, "\r\n\r\n") + 4, "POST|x=1&y=2|3|abc") == 0);

    /* D05 */
    serve("echo", "GET", "/cgi-bin/echo", NULL, resp, sizeof resp);
    assert(strcmp(strstr(resp, "\r\n\r\n") + 4, "GET||0|") == 0);

    /* D06: 0이 아닌 종료는 출력이 있어도 500 */
    serve("fail", "GET", "/cgi-bin/fail", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 500", 12) == 0);

    /* D07 */
    serve("missing", "GET", "/cgi-bin/missing", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 404", 12) == 0);
    serve("noexec", "GET", "/cgi-bin/noexec", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 403", 12) == 0);

    /* D08: 출력 없이 멈춘 CGI는 시간 제한 뒤 죽이고 500 */
    time_t start = time(NULL);
    serve("hang", "GET", "/cgi-bin/hang", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 500", 12) == 0);
    assert(time(NULL) - start < CGI_TIMEOUT_SEC + 3);

    /* D09: 출력을 닫고 계속 도는 CGI도 시간 제한 뒤 죽이고 500 */
    start = time(NULL);
    serve("linger", "GET", "/cgi-bin/linger", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 500", 12) == 0);
    assert(time(NULL) - start < CGI_TIMEOUT_SEC + 3);

    /* D15: 출력이 CGI_MAX_OUTPUT을 넘으면 죽이고 500 */
    serve("big", "GET", "/cgi-bin/big", NULL, resp, sizeof resp);
    assert(strncmp(resp, "HTTP/1.1 500", 12) == 0);

    char path[64];
    for (const char **n = (const char *[]){"echo", "fail", "noexec", "hang", "linger", "big", NULL}; *n; n++) {
        snprintf(path, sizeof path, "%s/%s", dir, *n);
        unlink(path);
    }
    rmdir(dir);
}

int main(void)
{
    test_parse();
    test_serve();
    return 0;
}
