/*
 * static_file.c - 정적 파일 경로 해석과 송신 (CS:APP3e 11.5.2, Tiny의 serve_static)
 * 설계: docs/static-file-serving/
 */
#include <strings.h>

#include "http_response.h"
#include "static_file.h"

const char *mime_type(const char *path)
{
    static const struct {
        const char *ext, *type;
    } table[] = {
        {"html", "text/html"}, {"css", "text/css"},   {"js", "text/javascript"}, {"txt", "text/plain"},
        {"png", "image/png"},  {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},    {"gif", "image/gif"},
    };
    const char *base = strrchr(path, '/');
    const char *dot = strrchr(base ? base : path, '.');
    if (dot != NULL)
        for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
            if (strcasecmp(dot + 1, table[i].ext) == 0)
                return table[i].type;
    return "application/octet-stream";
}

int static_resolve_path(const char *root, const char *uri, char *path, size_t size)
{
    size_t len = strcspn(uri, "?");

    /* 정확히 ".."인 조각만 거부합니다. "a..b"는 허용합니다. */
    for (size_t i = 0; i < len;) {
        size_t seg = strcspn(uri + i, "/?");
        if (seg == 2 && uri[i] == '.' && uri[i + 1] == '.')
            return 403;
        i += seg + 1;
    }

    const char *index = (len > 0 && uri[len - 1] == '/') ? "index.html" : "";
    int n = snprintf(path, size, "%s%.*s%s", root, (int)len, uri, index);
    if (n < 0 || (size_t)n >= size)
        return 400;
    return 0;
}

static int open_status(int err)
{
    switch (err) {
    case ENOENT:
    case ENOTDIR: return 404;
    case EACCES: return 403;
    case ENAMETOOLONG: return 400;
    default: return 500;
    }
}

int static_serve(int fd, const char *path)
{
    int filefd = open(path, O_RDONLY);
    if (filefd < 0)
        return http_send_error(fd, open_status(errno));

    struct stat sb;
    int status = 0;
    if (fstat(filefd, &sb) < 0)
        status = 500;
    else if (!S_ISREG(sb.st_mode))
        status = 403;
    if (status != 0) {
        close(filefd);
        return http_send_error(fd, status);
    }

    int rc = http_send_head(fd, 200, mime_type(path), (size_t)sb.st_size);
    char buf[MAXLINE];
    ssize_t n;
    while (rc == 0 && (n = read(filefd, buf, sizeof buf)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            rc = -1; /* 헤더를 이미 보냈으므로 연결을 끊음 */
        } else if (rio_writen(fd, buf, (size_t)n) != n) {
            rc = -1;
        }
    }
    close(filefd);
    return rc;
}
