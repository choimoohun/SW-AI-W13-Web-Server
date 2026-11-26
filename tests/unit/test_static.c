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
#include <unistd.h>      /* close, unlink, rmdir, symlink, getpid */
#include <sys/stat.h>    /* mkdir */
#include <limits.h>      /* PATH_MAX */

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static static_file_t rs(const char *uri, const char *root)
{
    return static_handler_resolve(uri, strlen(uri), root);
}

/* snprintf 대신 memcpy로 이어 붙인다. gcc -Werror=format-truncation(Linux) 회피. */
static void join2(char *dst, size_t cap, const char *a, const char *b)
{
    size_t na = strlen(a);
    size_t nb = strlen(b);
    if (na + nb + 1 > cap) { dst[0] = '\0'; return; }
    memcpy(dst, a, na);
    memcpy(dst + na, b, nb + 1);
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

    /* 디렉터리 index.html 이 doc_root 밖을 가리키는 심볼릭 링크인 경우.
     * 디렉터리 자체는 경계 안이라 통과하고, index.html 을 덧붙인 뒤에 링크를 따라간다. */
    {
        char abs_root[PATH_MAX];
        char outside[PATH_MAX + 32], sub[PATH_MAX + 32], link[PATH_MAX + 32];
        /* 심볼릭 링크 대상은 절대경로여야 한다. 상대경로면 링크 위치 기준으로 풀려 엉뚱한 파일을 가리킨다. */
        if (realpath(root, abs_root) == NULL) abs_root[0] = '\0';
        join2(outside, sizeof(outside), abs_root, "_outside_secret.txt");
        f = fopen(outside, "w"); if (f) { fputs("secret", f); fclose(f); }
        join2(sub, sizeof(sub), root, "/sub");
        mkdir(sub, 0700);
        join2(link, sizeof(link), sub, "/index.html");
        if (symlink(outside, link) == 0) {
            r = rs("/sub", root);
            CHECK(r.status == 403, "dir index symlink outside -> 403");
            if (r.file_fd >= 0) close(r.file_fd);
            r = rs("/sub/", root);
            CHECK(r.status == 403, "dir index symlink outside (trailing slash) -> 403");
            if (r.file_fd >= 0) close(r.file_fd);
            unlink(link);
        }
        rmdir(sub);
        unlink(outside);
    }

    /* 정상 디렉터리 index.html 은 그대로 서빙되어야 한다. */
    {
        char sub[PATH_MAX + 32], idx[PATH_MAX + 32];
        join2(sub, sizeof(sub), root, "/dir");
        mkdir(sub, 0700);
        join2(idx, sizeof(idx), sub, "/index.html");
        f = fopen(idx, "w"); if (f) { fputs("<html>dir</html>", f); fclose(f); }
        r = rs("/dir", root);
        CHECK(r.status == 200 && r.mime && strcmp(r.mime, "text/html") == 0,
              "normal dir index -> 200 text/html");
        if (r.file_fd >= 0) close(r.file_fd);
        unlink(idx);
        rmdir(sub);
    }

    /* 정리 */
    snprintf(p, sizeof(p), "%s/index.html", root); unlink(p);
    snprintf(p, sizeof(p), "%s/hello.txt", root); unlink(p);
    rmdir(root);

    if (failures) { printf("test_static: %d FAILED\n", failures); return 1; }
    printf("test_static: all passed\n");
    return 0;
}
