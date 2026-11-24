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

    /* S04 */
    assert(static_resolve_path("/srv/www", "/style.css", path, 16) == 400);
    /* 경계: 정확히 맞으면 성공, 1바이트 모자라면 400 */
    assert(static_resolve_path("/srv/www", "/style.css", path, 19) == 0);
    assert(static_resolve_path("/srv/www", "/style.css", path, 18) == 400);
}

int main(void)
{
    test_mime();
    test_resolve();
    return 0;
}
