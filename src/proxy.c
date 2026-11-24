#include "proxy.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { GATEWAY_ERROR = 502, GATEWAY_TIMEOUT = 504, TIMEOUT_MS = 3000, HEADER_LIMIT = 4096 };

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_ready(int fd, short events, int64_t deadline)
{
    struct pollfd socket = { .fd = fd, .events = events };
    for (;;) {
        int64_t now = monotonic_ms();
        if (now < 0) return GATEWAY_ERROR;
        if (now >= deadline) return GATEWAY_TIMEOUT;

        int ready = poll(&socket, 1, (int)(deadline - now));
        if (ready > 0) return (socket.revents & POLLNVAL) ? GATEWAY_ERROR : 0;
        if (ready == 0) return GATEWAY_TIMEOUT;
        if (errno != EINTR) return GATEWAY_ERROR;
    }
}

static int connect_backend(int fd, const struct sockaddr_in *address, int64_t deadline)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) return GATEWAY_ERROR;
    if (connect(fd, (const struct sockaddr *)address, sizeof *address) == 0) return 0;
    if (errno == ETIMEDOUT) return GATEWAY_TIMEOUT;
    if (errno != EINPROGRESS && errno != EINTR) return GATEWAY_ERROR;

    int status = wait_ready(fd, POLLOUT, deadline);
    if (status != 0) return status;

    int error;
    socklen_t length = sizeof error;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == -1) return GATEWAY_ERROR;
    if (error == ETIMEDOUT) return GATEWAY_TIMEOUT;
    return error == 0 ? 0 : GATEWAY_ERROR;
}

static int send_bytes(int fd, const char *data, size_t length, int64_t deadline)
{
    while (length > 0) {
        int status = wait_ready(fd, POLLOUT, deadline);
        if (status != 0) return status;

        ssize_t count = write(fd, data, length);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return GATEWAY_ERROR;
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

static int receive_bytes(int fd, char *data, size_t length, size_t *received, int64_t deadline)
{
    for (;;) {
        int status = wait_ready(fd, POLLIN, deadline);
        if (status != 0) return status;

        ssize_t count = read(fd, data, length);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) return GATEWAY_ERROR; // 길이에 못 미친 채 EOF가 오면 잘린 응답이다.
        *received = (size_t)count;
        return 0;
    }
}

static bool parse_length(const char *value, size_t limit, size_t *length)
{
    if (*value == '\0') return false;
    *length = 0;
    for (; *value != '\0'; value++) {
        if (*value < '0' || *value > '9') return false;
        size_t digit = (size_t)(*value - '0');
        if (*length > (limit - digit) / 10) return false;
        *length = *length * 10 + digit;
    }
    return true;
}

static bool parse_response_headers(char *header, struct proxy_response *response)
{
    char *end = strstr(header, "\r\n");
    if (end == NULL || end - header < 13 || strncmp(header, "HTTP/1.1 ", 9) != 0 ||
        header[9] < '2' || header[9] > '5' ||
        header[10] < '0' || header[10] > '9' ||
        header[11] < '0' || header[11] > '9' || header[12] != ' ') return false;
    response->status = (header[9] - '0') * 100 + (header[10] - '0') * 10 + header[11] - '0';
    for (const char *p = header + 13; p < end; p++) {
        if ((unsigned char)*p < 32 || (unsigned char)*p >= 127) return false;
    }
    // 이 백엔드 계약은 Content-Length로 끝나는 본문 응답만 사용한다.
    if (response->status == 204 || response->status == 304) return false;

    bool has_length = false;
    bool has_type = false;
    bool has_allow = false;
    for (char *line = end + 2; (end = strstr(line, "\r\n")) != NULL; line = end + 2) {
        if (line == end) return has_length && has_type;
        *end = '\0';
        char *colon = strchr(line, ':');
        if (colon == NULL || colon == line) return false;
        *colon = '\0';
        if (line[strspn(line, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                              "0123456789!#$%&'*+-.^_`|~")] != '\0') return false;
        char *value = colon + 1;
        value += strspn(value, " \t");
        char *tail = end;
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = '\0';
        for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
            if (*p < 32 || *p >= 127) return false;
        }
        if (strcasecmp(line, "Transfer-Encoding") == 0) return false;
        if (strcasecmp(line, "Content-Length") == 0) {
            if (has_length || !parse_length(value, sizeof response->body, &response->length))
                return false;
            has_length = true;
        } else if (strcasecmp(line, "Content-Type") == 0) {
            if (has_type || *value == '\0' || strlen(value) >= sizeof response->type) return false;
            strcpy(response->type, value);
            has_type = true;
        } else if (strcasecmp(line, "Allow") == 0) {
            if (has_allow || strlen(value) >= sizeof response->allow) return false;
            strcpy(response->allow, value);
            has_allow = true;
        }
    }
    return false;
}

static int read_response(int fd, int64_t deadline, struct proxy_response *response)
{
    char header[HEADER_LIMIT + 1];
    size_t used = 0;
    size_t header_end = 0;
    size_t next = 3;
    while (used < HEADER_LIMIT && header_end == 0) {
        size_t received;
        int status = receive_bytes(fd, header + used, HEADER_LIMIT - used, &received, deadline);
        if (status != 0) return status;
        used += received;
        for (; next < used; next++) {
            if (memcmp(header + next - 3, "\r\n\r\n", 4) == 0) {
                header_end = next + 1;
                break;
            }
        }
    }
    if (header_end == 0 || memchr(header, '\0', header_end) != NULL) return GATEWAY_ERROR;

    // 같은 read()에서 받은 본문은 헤더를 문자열로 바꾸기 전에 보존한다.
    size_t body_used = used - header_end;
    memcpy(response->body, header + header_end, body_used);
    header[header_end] = '\0';
    if (!parse_response_headers(header, response) || body_used > response->length)
        return GATEWAY_ERROR;

    while (body_used < response->length) {
        size_t received;
        int status = receive_bytes(fd, response->body + body_used,
                                   response->length - body_used, &received, deadline);
        if (status != 0) return status;
        body_used += received;
    }
    return 0;
}

int proxy_request(const struct sockaddr_in *backend, const char *method, const char *target,
                  const char *type, const char *body, size_t length,
                  struct proxy_response *response)
{
    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &backend->sin_addr, address, sizeof address) == NULL) return GATEWAY_ERROR;

    char header[HEADER_LIMIT + 256];
    int count = snprintf(header, sizeof header,
                         "%s %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n"
                         "Content-Length: %zu\r\n%s%s%s\r\n",
                         method, target, address, (unsigned)ntohs(backend->sin_port), length,
                         type ? "Content-Type: " : "", type ? type : "", type ? "\r\n" : "");
    if (count < 0 || (size_t)count >= sizeof header) return GATEWAY_ERROR;

    int64_t now = monotonic_ms();
    if (now < 0) return GATEWAY_ERROR;
    int64_t deadline = now + TIMEOUT_MS;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return GATEWAY_ERROR;

    int status = connect_backend(fd, backend, deadline);
    if (status == 0) status = send_bytes(fd, header, (size_t)count, deadline);
    if (status == 0) status = send_bytes(fd, body, length, deadline);
    if (status == 0) status = read_response(fd, deadline, response);
    close(fd);
    return status;
}
