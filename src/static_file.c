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

int static_has_bad_segments(const char *uri)
{
    size_t len = strcspn(uri, "?");

    /* "..", ".", 빈 조각("//")을 거부해 한 파일을 가리키는 URI를 하나로 만듭니다. "a..b"는 허용합니다.
       그래야 "//cgi-bin/x"처럼 접두사 검사를 피해 CGI 프로그램을 정적 파일로 받아 가지 못합니다. */
    for (size_t i = 0; i < len;) {
        size_t seg = strcspn(uri + i, "/?");
        int dotdot = seg == 2 && uri[i] == '.' && uri[i + 1] == '.';
        int dot = seg == 1 && uri[i] == '.';
        int empty = seg == 0 && i > 0; /* 맨 앞 '/' 앞의 빈 조각은 정상 */
        if (dotdot || dot || empty)
            return 1;
        i += seg + 1;
    }
    return 0;
}

int static_resolve_path(const char *root, const char *uri, char *path, size_t size)
{
    size_t len = strcspn(uri, "?");

    if (static_has_bad_segments(uri))
        return 403;
    const char *index = (len > 0 && uri[len - 1] == '/') ? "index.html" : "";
    int n = snprintf(path, size, "%s%.*s%s", root, (int)len, uri, index);
    if (n < 0 || (size_t)n >= size)
        return 400;
    return 0;
}

int static_errno_status(int err)
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
    /* O_NONBLOCK: FIFO를 열 때 쓰는 쪽을 기다리며 워커가 멈추지 않게 합니다.
       O_CLOEXEC: 다른 워커가 fork한 CGI에 이 파일이 새지 않게 합니다. */
    int filefd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (filefd < 0)
        return http_send_error(fd, static_errno_status(errno));

    struct stat sb;
    int status = 0, fl;
    if (fstat(filefd, &sb) < 0)
        status = 500;
    else if (!S_ISREG(sb.st_mode))
        status = 403;
    else if ((fl = fcntl(filefd, F_GETFL)) < 0 || fcntl(filefd, F_SETFL, fl & ~O_NONBLOCK) < 0)
        status = 500;
    if (status != 0) {
        close(filefd);
        return http_send_error(fd, status);
    }

    int rc = http_send_head(fd, 200, mime_type(path), (size_t)sb.st_size);
    char buf[MAXLINE];
    off_t left = sb.st_size; /* Content-Length로 보낸 크기만큼만 보냄 */
    while (rc == 0 && left > 0) {
        size_t want = left < (off_t)sizeof buf ? (size_t)left : sizeof buf;
        ssize_t n = read(filefd, buf, want);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || rio_writen(fd, buf, (size_t)n) != n)
            rc = -1; /* read 실패나 파일이 줄어든 EOF. 헤더를 이미 보냈으므로 연결을 끊음 */
        else
            left -= n;
    }
    close(filefd);
    return rc;
}
