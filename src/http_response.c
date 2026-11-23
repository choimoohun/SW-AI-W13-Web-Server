/*
 * http_response.c - HTTP 응답 줄·헤더와 오류 응답 (CS:APP3e 11.5.3, 그림 11.25)
 * 설계: docs/http-response/
 */
#include "http_response.h"

const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad request";
    case 403: return "Forbidden";
    case 404: return "Not found";
    case 500: return "Internal server error";
    case 501: return "Not implemented";
    case 505: return "HTTP version not supported";
    default: return NULL;
    }
}

int http_format_head(char *buf, size_t size, int status, const char *content_type, size_t content_length)
{
    const char *text = http_status_text(status);
    if (text == NULL)
        return -1;
    int n = snprintf(buf, size,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, text, content_type, content_length);
    if (n < 0 || (size_t)n >= size)
        return -1;
    return n;
}

int http_send_head(int fd, int status, const char *content_type, size_t content_length)
{
    char buf[MAXLINE];
    int n = http_format_head(buf, sizeof buf, status, content_type, content_length);
    if (n < 0 || rio_writen(fd, buf, (size_t)n) != n)
        return -1;
    return 0;
}

int http_send_error(int fd, int status)
{
    const char *text = http_status_text(status);
    if (text == NULL)
        return -1;
    char body[MAXLINE];
    int n = snprintf(body, sizeof body, "<html><body><h1>%d %s</h1></body></html>\n", status, text);
    if (n < 0 || (size_t)n >= sizeof body)
        return -1;
    if (http_send_head(fd, status, "text/html", (size_t)n) < 0 || rio_writen(fd, body, (size_t)n) != n)
        return -1;
    return 0;
}
