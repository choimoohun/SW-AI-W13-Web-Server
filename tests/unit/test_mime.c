/* test_mime.c — MIME 매핑 단위 테스트.
 *
 *  - .html .css .js .png .json → 정해진 MIME
 *  - 모르는 확장자           → application/octet-stream
 *
 * 빌드: cc $(CFLAGS) -o test_mime tests/unit/test_mime.c src/static_handler.c
 */
#include "static_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { printf("FAIL: %s\n", msg); failures++; }          \
    } while (0)

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fputs(content, f);
    fclose(f);
}

int main(void)
{
    char root[64];
    snprintf(root, sizeof(root), ".static_mime_%ld", (long)getpid());
    if (mkdir(root, 0700) != 0 && errno != EEXIST) {
        perror("mkdir");
        return 1;
    }

    static const char *names[] = { "a.html", "b.css", "c.js", "d.json", "e.png", "f.xyz" };
    static const char *expected[] = {
        "text/html", "text/css", "application/javascript", "application/json",
        "image/png", "application/octet-stream",
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char uri[256];
        char p[256];
        char msg[128];
        snprintf(uri, sizeof(uri), "/%s", names[i]);
        snprintf(p, sizeof(p), "%s/%s", root, names[i]);
        write_file(p, "x");
        static_file_t r = static_handler_resolve(uri, strlen(uri), root);
        char exp[64];
        snprintf(exp, sizeof(exp), "%s", expected[i]);
        snprintf(msg, sizeof(msg), "MIME for /%s", names[i]);
        CHECK(r.status == 200 && r.mime != NULL &&
                  strcmp(r.mime, exp) == 0,
              msg);
        if (r.file_fd >= 0) close(r.file_fd);
    }

    /* 정리 */
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char p[256];
        snprintf(p, sizeof(p), "%s/%s", root, names[i]);
        unlink(p);
    }
    rmdir(root);

    if (failures) {
        printf("test_mime: %d FAILED\n", failures);
        return 1;
    }
    printf("test_mime: all checks passed\n");
    return 0;
}
