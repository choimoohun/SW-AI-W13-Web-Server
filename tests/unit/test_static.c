/*
 * test_static.c — static_handler_resolve 단위 테스트 (경로 보안 + MIME + 404).
 * 임시 doc_root 를 만들어 자체 완결적으로 검증한다(www/ 의존 없음).
 * 요구사항 §7: 경로 보안 검사, MIME 매핑 단위 검증.
 * 빌드: cc $(CFLAGS) -o test_static tests/unit/test_static.c src/static_handler.c
 */
#include "static_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* close, unlink, rmdir, getpid */
#include <sys/stat.h>    /* mkdir */

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static static_file_t rs(const char *uri, const char *root)
{
    return static_handler_resolve(uri, strlen(uri), root);
}

int main(void)
{
    /* mkdtemp 대신 mkdir + pid 기반 이름(포터블, macOS feature-macro 이슈 회피). */
    char root[64];
    snprintf(root, sizeof(root), ".static_test_%ld", (long)getpid());
    mkdir(root, 0700);   /* 이미 있으면 무시 */

    char p[256];
    FILE *f;
    snprintf(p, sizeof(p), "%s/index.html", root);
    f = fopen(p, "w"); if (f) { fputs("<html>hi</html>", f); fclose(f); }
    snprintf(p, sizeof(p), "%s/hello.txt", root);
    f = fopen(p, "w"); if (f) { fputs("hello", f); fclose(f); }

    static_file_t r;

    r = rs("/", root);
    CHECK(r.status == 200 && r.mime && strcmp(r.mime, "text/html") == 0, "/ -> 200 text/html");
    if (r.file_fd >= 0) close(r.file_fd);

    r = rs("/hello.txt", root);
    CHECK(r.status == 200 && r.mime && strcmp(r.mime, "text/plain") == 0, "hello.txt -> text/plain");
    if (r.file_fd >= 0) close(r.file_fd);

    r = rs("/nope", root);
    CHECK(r.status == 404, "missing -> 404");

    r = rs("/../etc/passwd", root);
    CHECK(r.status == 403, "traversal -> 403");

    r = rs("/%2e%2e/etc/passwd", root);
    CHECK(r.status == 403, "encoded traversal -> 403");

    r = rs("/a..b", root);
    CHECK(r.status == 404, "a..b is legal name (not traversal) -> 404");

    /* 정리 */
    snprintf(p, sizeof(p), "%s/index.html", root); unlink(p);
    snprintf(p, sizeof(p), "%s/hello.txt", root); unlink(p);
    rmdir(root);

    if (failures) { printf("test_static: %d FAILED\n", failures); return 1; }
    printf("test_static: all passed\n");
    return 0;
}
