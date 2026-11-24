#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_HEADER_BYTES = 4096, MAX_BODY_BYTES = 8192 };

struct request {
    char *method;
    char *target;
    size_t body_length;
};

static ssize_t read_bytes(int fd, char *buffer, size_t length)
{
    ssize_t count;

    do {
        count = read(fd, buffer, length);
    } while (count < 0 && errno == EINTR);

    return count;
}

static int read_headers(int fd, char *buffer, size_t *used, size_t *header_end)
{
    size_t next = 3;

    while (*used < MAX_HEADER_BYTES) {
        ssize_t count = read_bytes(fd, buffer + *used, MAX_HEADER_BYTES - *used);
        if (count <= 0) {
            return 400;
        }
        *used += (size_t)count;

        for (; next < *used; next++) {
            if (memcmp(buffer + next - 3, "\r\n\r\n", 4) == 0) {
                *header_end = next + 1;       // 빈 줄 뒤부터 본문이다.
                return 200;
            }
        }
    }

    return 431;
}

static char *line_end(char *line, char *end)
{
    for (char *p = line; end - p >= 2; p++) {
        if (p[0] == '\r' && p[1] == '\n') {
            return p;
        }
    }
    return NULL;
}

static bool is_token_byte(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') ||
           (byte != '\0' && strchr("!#$%&'*+-.^_`|~", byte) != NULL);
}

static int parse_request_line(char *line, char *end, struct request *request)
{
    char *first = memchr(line, ' ', (size_t)(end - line));
    if (first == NULL || first == line) {
        return 400;
    }
    char *second = memchr(first + 1, ' ', (size_t)(end - first - 1));
    if (second == NULL || second == first + 1 || second + 1 == end) {
        return 400;
    }

    for (char *p = line; p < first; p++) {
        if (!is_token_byte((unsigned char)*p)) {
            return 400;
        }
    }
    char *target = first + 1;
    if (*target != '/') {
        return 400;
    }
    for (char *p = target; p < second; p++) {
        unsigned char byte = (unsigned char)*p;
        if (byte <= ' ' || byte >= 0x7f || byte == '#') {
            return 400;
        }
    }

    char *version = second + 1;
    if (end - version != 8 || memcmp(version, "HTTP/", 5) != 0 ||
        version[5] < '0' || version[5] > '9' || version[6] != '.' ||
        version[7] < '0' || version[7] > '9') {
        return 400;
    }
    if (version[5] != '1' || version[7] != '1') {
        return 505;
    }

    *first = '\0';                       // 메서드와 요청 대상을 독립된 문자열로 만든다.
    *second = '\0';
    request->method = line;
    request->target = target;
    return 200;
}

static int parse_body_length(const char *value, const char *end, size_t *length)
{
    if (value == end) {
        return 400;
    }
    for (const char *p = value; p < end; p++) {
        if (*p < '0' || *p > '9') {
            return 400;
        }
        size_t digit = (size_t)(*p - '0');
        if (*length > (MAX_BODY_BYTES - digit) / 10) {
            return 413;
        }
        *length = *length * 10 + digit;
    }
    return 200;
}

static bool header_is(const char *name, const char *colon, const char *expected)
{
    size_t length = (size_t)(colon - name);
    return length == strlen(expected) && strncasecmp(name, expected, length) == 0;
}

static int parse_headers(char *line, char *end, struct request *request)
{
    bool has_host = false;
    bool has_length = false;
    bool has_transfer_encoding = false;
    bool has_expect = false;

    for (;;) {
        char *stop = line_end(line, end);
        if (stop == NULL) {
            return 400;
        }
        if (stop == line) {
            break;
        }
        char *colon = memchr(line, ':', (size_t)(stop - line));
        if (colon == NULL || colon == line) {
            return 400;
        }
        for (char *p = line; p < colon; p++) {
            if (!is_token_byte((unsigned char)*p)) {
                return 400;
            }
        }

        char *value = colon + 1;
        char *value_end = stop;
        while (value < value_end && (*value == ' ' || *value == '\t')) {
            value++;
        }
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            value_end--;
        }
        for (char *p = value; p < value_end; p++) {
            unsigned char byte = (unsigned char)*p;
            if ((byte < ' ' && byte != '\t') || byte == 0x7f) {
                return 400;
            }
        }

        if (header_is(line, colon, "Host")) {
            if (has_host || value == value_end) {
                return 400;
            }
            for (char *p = value; p < value_end; p++) {
                unsigned char byte = (unsigned char)*p;
                if (byte <= ' ' || byte >= 0x7f || strchr(",/#?@", byte) != NULL) {
                    return 400;
                }
            }
            has_host = true;
        } else if (header_is(line, colon, "Content-Length")) {
            if (has_length) {
                return 400;
            }
            has_length = true;
            int status = parse_body_length(value, value_end, &request->body_length);
            if (status != 200) {
                return status;
            }
        } else if (header_is(line, colon, "Transfer-Encoding")) {
            has_transfer_encoding = true;
        } else if (header_is(line, colon, "Expect")) {
            has_expect = true;
        }
        line = stop + 2;
    }

    if (!has_host || (has_transfer_encoding && has_length)) {
        return 400;
    }
    if (has_transfer_encoding) {
        return 501;
    }
    if (has_expect) {
        return 417;
    }
    return 200;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    while (length > 0) {
        ssize_t written = write(fd, buffer, length);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        buffer += written;
        length -= (size_t)written;
    }
    return 0;
}

static int respond_headers(int fd, int status, const char *reason, const char *type,
                           size_t length)
{
    char headers[256];
    int bytes = snprintf(headers, sizeof headers,
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                         "Content-Length: %zu\r\nConnection: close\r\n%s\r\n",
                         status, reason, type, length,
                         status == 405 ? "Allow: GET, POST\r\n" : "");
    if (bytes < 0 || (size_t)bytes >= sizeof headers ||
        write_all(fd, headers, (size_t)bytes) < 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int respond(int fd, int status, const char *reason, const char *type,
                   const char *body, size_t length)
{
    if (respond_headers(fd, status, reason, type, length) != EXIT_SUCCESS ||
        write_all(fd, body, length) < 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int respond_error(int fd, int status)
{
    const char *reason;

    switch (status) {
    case 403: reason = "Forbidden"; break;
    case 404: reason = "Not Found"; break;
    case 405: reason = "Method Not Allowed"; break;
    case 413: reason = "Content Too Large"; break;
    case 417: reason = "Expectation Failed"; break;
    case 431: reason = "Request Header Fields Too Large"; break;
    case 500: reason = "Internal Server Error"; break;
    case 501: reason = "Not Implemented"; break;
    case 505: reason = "HTTP Version Not Supported"; break;
    default: reason = "Bad Request"; break;
    }

    return respond(fd, status, reason, "text/plain", reason, strlen(reason));
}

static int hex_digit(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    return -1;
}

static int decode_path(char *path)
{
    const char *end = path + strcspn(path, "?");   // 쿼리는 파일 경로에서 제외한다.
    char *out = path;

    for (const char *p = path; p < end; p++) {
        unsigned char byte = (unsigned char)*p;
        if (byte == '%') {
            if (end - p < 3) return 400;
            int high = hex_digit((unsigned char)p[1]);
            int low = hex_digit((unsigned char)p[2]);
            if (high < 0 || low < 0) return 400;
            byte = (unsigned char)(high * 16 + low);
            if (byte == '/') return 403;            // 인코딩된 구분자로 경로를 바꾸지 못하게 한다.
            p += 2;
        }
        if (byte == '\\') return 403;
        if (byte < ' ' || byte == 0x7f) return 400;
        *out++ = (char)byte;
    }
    *out = '\0';
    return 200;
}

static const char *mime_type(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension == NULL) return "application/octet-stream";
    if (strcasecmp(extension, ".html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(extension, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(extension, ".js") == 0) return "text/javascript; charset=utf-8";
    if (strcasecmp(extension, ".png") == 0) return "image/png";
    if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0) return "image/jpeg";
    return "application/octet-stream";
}

static int open_static_file(const char *root, char *path, int *file)
{
    int dir = open(root, O_RDONLY | O_DIRECTORY);
    if (dir < 0) return 500;

    for (;;) {
        char *slash = strchr(path, '/');
        if (slash != NULL) *slash = '\0';
        if (*path == '\0' || strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
            close(dir);
            return 403;
        }

        int flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK;
        if (slash != NULL) flags |= O_DIRECTORY;
        int next = openat(dir, path, flags);       // 직전 폴더 안에서만 다음 경로를 연다.
        int error = errno;
        if (next < 0 && error == ENOTDIR) {
            struct stat entry;
            if (fstatat(dir, path, &entry, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(entry.st_mode)) {
                error = ELOOP;
            }
        }
        close(dir);

        if (next < 0) {
            if (error == EACCES || error == EPERM || error == ELOOP) return 403;
            if (error == ENOENT || error == ENOTDIR) return 404;
            return 500;
        }
        if (slash == NULL) {
            *file = next;
            return 200;
        }
        dir = next;
        path = slash + 1;
    }
}

static int serve_static(int connfd, const char *root, char *target)
{
    int status = decode_path(target);
    if (status != 200) return respond_error(connfd, status);

    char index[] = "index.html";
    char *path = target[1] == '\0' ? index : target + 1;
    const char *type = mime_type(path);
    int file;
    status = open_static_file(root, path, &file);
    if (status != 200) return respond_error(connfd, status);

    struct stat info;
    if (fstat(file, &info) < 0 || info.st_size < 0 || (uintmax_t)info.st_size > SIZE_MAX) {
        close(file);
        return respond_error(connfd, 500);
    }
    if (!S_ISREG(info.st_mode)) {
        close(file);
        return respond_error(connfd, 403);
    }

    size_t remaining = (size_t)info.st_size;
    int result = respond_headers(connfd, 200, "OK", type, remaining);
    char chunk[4096];
    while (result == EXIT_SUCCESS && remaining > 0) {
        size_t limit = remaining < sizeof chunk ? remaining : sizeof chunk;
        ssize_t count = read_bytes(file, chunk, limit);
        if (count <= 0 || write_all(connfd, chunk, (size_t)count) < 0) {
            result = EXIT_FAILURE;                // 헤더를 보낸 뒤라면 연결을 닫아 종료한다.
        } else {
            remaining -= (size_t)count;
        }
    }
    close(file);
    return result;
}

int handle_http_client(int connfd, const char *document_root)
{
    char buffer[MAX_HEADER_BYTES + MAX_BODY_BYTES];
    struct request request = {0};
    size_t used = 0;
    size_t header_end;

    int status = read_headers(connfd, buffer, &used, &header_end);
    if (status != 200) {
        return respond_error(connfd, status);
    }

    char *end = buffer + header_end;
    char *request_end = line_end(buffer, end);
    if (request_end == NULL) {
        return respond_error(connfd, 400);
    }
    status = parse_request_line(buffer, request_end, &request);
    if (status == 200) {
        status = parse_headers(request_end + 2, end, &request);
    }
    if (status != 200) {
        return respond_error(connfd, status);
    }

    bool post = strcmp(request.method, "POST") == 0;
    if (!post && strcmp(request.method, "GET") != 0) {
        return respond_error(connfd, 405);
    }

    char *body = end;
    size_t received = used - header_end;       // 헤더와 함께 받은 본문부터 사용한다.
    while (received < request.body_length) {
        ssize_t count = read_bytes(connfd, body + received, request.body_length - received);
        if (count <= 0) {
            return respond_error(connfd, 400);
        }
        received += (size_t)count;
    }

    if (post) {
        return respond(connfd, 200, "OK", "application/octet-stream", body, request.body_length);
    }
    return serve_static(connfd, document_root, request.target);
}
