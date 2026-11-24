/*
 * http_response.c — HTTP 응답 직렬화 + 전송. 파싱/파일 탐색은 맡지 않는다.
 *
 * [핵심 원리 — 발표용]
 * - 응답 구조: 상태줄 → 헤더(Content-Type/Content-Length/Connection) → 빈 줄(\r\n) → 본문.
 * - Content-Length = 본문 '바이트 수'(문자 수 아님). 실제 전송량과 반드시 일치.
 * - send 는 부분 성공 가능 → send_all 로 남은 바이트 반복(EINTR 재시도). EPIPE 는 오류 반환.
 *   (SIGPIPE 는 main 에서 SIG_IGN — 서버가 죽지 않게.)
 * - 본문은 포인터+바이트 길이로 다뤄 바이너리 보존(strlen 금지).
 * - 큰 파일은 read(file_fd,16KB)→send_all 청크 반복. 전체 파일을 메모리에 올리지 않는다.
 * 요구사항: FR-03, NFR-01~03.
 */

#include "http_response.h"
#include <unistd.h>      /* read */
#include <sys/socket.h>  /* send */
#include <errno.h>
#include <stdio.h>       /* snprintf */

/* 남은 바이트를 모두 전송. 부분 쓰기/EINTR 처리. 실패 -1, 성공 0. */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static const char *reason_phrase(int status)
{
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default:  return "Error";
    }
}

void http_response_send_error(int client_fd, int status)
{
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, reason_phrase(status));
    if (n < 0 || (size_t)n >= sizeof(hdr)) return;
    send_all(client_fd, hdr, (size_t)n);
}

void http_response_send_file(int client_fd, const char *mime, int file_fd, long size)
{
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     mime, size);
    if (n < 0 || (size_t)n >= sizeof(hdr)) return;
    if (send_all(client_fd, hdr, (size_t)n) < 0) return;

    /* 본문 청크 전송: 16KB 씩 read → send_all. 전체 파일 메모리 적재 없음. */
    char chunk[16384];
    long sent = 0;
    while (sent < size) {
        ssize_t r = read(file_fd, chunk, sizeof(chunk));
        if (r < 0) {
            if (errno == EINTR) continue;
            return;   /* 읽기 오류 */
        }
        if (r == 0) break;   /* 파일이 size 보다 짧음(변경됨) — 성공 처리 안 함 */
        if (send_all(client_fd, chunk, (size_t)r) < 0) return;
        sent += r;
    }
    /* sent != size 면 Content-Length 와 불일치(파일 변경 등). 헤더는 이미 전송됨. */
}
