/* test_static_file.c - static_file.c 단위 테스트 (docs/static-file-serving/test-cases.md) */
#undef NDEBUG
#include <assert.h>

#include "static_file.h"

static void test_mime(void) /* S01 */
{
    assert(strcmp(mime_type("a.html"), "text/html") == 0);
    assert(strcmp(mime_type("A.PNG"), "image/png") == 0);
    assert(strcmp(mime_type("x.jpeg"), "image/jpeg") == 0);
    assert(strcmp(mime_type("a.jpg"), "image/jpeg") == 0);
    assert(strcmp(mime_type("x.js"), "text/javascript") == 0);
    assert(strcmp(mime_type("noext"), "application/octet-stream") == 0);
    assert(strcmp(mime_type("v1.0/file"), "application/octet-stream") == 0);
}

static void test_resolve(void)
{
    char path[MAXLINE];

    /* S02 */
    assert(static_resolve_path("/srv/www", "/", path, sizeof path) == 0);
    assert(strcmp(path, "/srv/www/index.html") == 0);
    assert(static_resolve_path("/srv/www", "/style.css?v=2", path, sizeof path) == 0);
    assert(strcmp(path, "/srv/www/style.css") == 0);
    assert(static_resolve_path("/srv/www", "/a..b.html", path, sizeof path) == 0);
    assert(strcmp(path, "/srv/www/a..b.html") == 0);

    /* S03 */
    assert(static_resolve_path("/srv/www", "/../etc/passwd", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/a/..", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/a/../b", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/a/..?x", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/a?/../b", path, sizeof path) == 0); /* '?' 뒤는 검사하지 않음 */
    assert(strcmp(path, "/srv/www/a") == 0);

    /* S18: 프록시 분기 전에도 쓰는 경로 조각 검사 */
    assert(static_has_bad_segments("/api/a..b") == 0);
    assert(static_has_bad_segments("/api?x=/../") == 0);
    assert(static_has_bad_segments("/api/../x") == 1);
    assert(static_has_bad_segments("/api/./x") == 1);
    assert(static_has_bad_segments("//api") == 1);

    /* S17: 같은 파일의 다른 표기는 거부. "/cgi-bin/" 접두사 검사를 피하지 못하게 함 */
    assert(static_resolve_path("/srv/www", "//cgi-bin/x", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/./cgi-bin/x", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/cgi-bin//x", path, sizeof path) == 403);
    assert(static_resolve_path("/srv/www", "/dir/", path, sizeof path) == 0); /* 끝 '/'는 index.html */
    assert(static_resolve_path("/srv/www", "/.hidden", path, sizeof path) == 0);

    /* S04 */
    assert(static_resolve_path("/srv/www", "/style.css", path, 16) == 400);
    /* 경계: 정확히 맞으면 성공, 1바이트 모자라면 400 */
    assert(static_resolve_path("/srv/www", "/style.css", path, 19) == 0);
    assert(static_resolve_path("/srv/www", "/style.css", path, 18) == 400);
}

static void test_fifo(void) /* S15 */
{
    char dir[] = "/tmp/test_static_fileXXXXXX", fifo[64], buf[64];
    int sv[2];

    assert(mkdtemp(dir) != NULL);
    snprintf(fifo, sizeof fifo, "%s/pipe", dir);
    assert(mkfifo(fifo, 0600) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    alarm(5); /* 예전처럼 open()에서 막히면 SIGALRM으로 실패 */
    static_serve(sv[0], fifo);
    alarm(0);
    close(sv[0]);
    ssize_t n = read(sv[1], buf, sizeof buf - 1);
    assert(n > 0);
    buf[n] = '\0';
    assert(strncmp(buf, "HTTP/1.1 403", 12) == 0);

    close(sv[1]);
    unlink(fifo);
    rmdir(dir);
}

int main(void)
{
    test_mime();
    test_resolve();
    test_fifo();
    return 0;
}
