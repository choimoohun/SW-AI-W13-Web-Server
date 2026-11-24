/*
 * connection.c — 연결 상태 머신 (STEP5).
 *
 * 블로킹 read/send를 쓰지 않는다. 워커 이벤트 루프가 READ/WRITE 이벤트에 맞춰
 * feed/flush를 호출하고, send_buf/파일 잔여가 남았으면 WRITE 관심으로 전환한다.
 * keep-alive 요청은 같은 컨텍스트에서 여러 번 처리된다.
 */
#include "connection.h"

#include "http_parser.h"
#include "http_response.h"
#include "static_handler.h"

#include <arpa/inet.h>   /* inet_ntop */
#include <netinet/in.h>  /* struct sockaddr_in */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>      /* malloc, realloc, free */
#include <string.h>      /* memcpy, memmove, memcmp */
#include <sys/socket.h>  /* send */
#include <unistd.h>      /* read, close */

static void queue_error(connection_t *c, int status, int keep_alive)
{
    size_t off = c->send_len;
    size_t n = http_response_build_error(c->send_buf + off,
                                         sizeof(c->send_buf) - off,
                                         status, keep_alive);
    if (n > 0) {
        c->send_len += n;
    } else {
        c->closing = 1;   /* 버퍼 부족 → 그냥 닫는다 */
    }
}

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;

static void access_log(const connection_t *c, const http_request_t *req,
                       long latency_ms)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_REALTIME, &now);
    pthread_mutex_lock(&g_log_mu);
    fprintf(stderr, "[%ld.%03ld] [w%d] [%s] \"%.*s %.*s\" %d %zu %ldms\n",
            (long)now.tv_sec, now.tv_nsec / 1000000L, c->worker_id, c->peer_ip,
            (int)req->method_len, req->method, (int)req->uri_len, req->uri,
            c->status, c->send_len, latency_ms);
    pthread_mutex_unlock(&g_log_mu);
}

void connection_init(connection_t *c, int fd, const server_config_t *cfg,
                     int worker_id)
{
    struct sockaddr_storage addr;
    socklen_t alen = sizeof(addr);

    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->cfg = cfg;
    c->file_fd = -1;
    c->last_active = time(NULL);
    c->worker_id = worker_id;
    (void)clock_gettime(CLOCK_MONOTONIC, &c->req_start);
    c->peer_ip[0] = '\0';
    c->recv_buf = malloc(CONN_RECV_CAP);
    c->recv_cap = (c->recv_buf != NULL) ? CONN_RECV_CAP : 0;
    if (getpeername(fd, (struct sockaddr *)&addr, &alen) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *in = (struct sockaddr_in *)&addr;
            (void)inet_ntop(AF_INET, &in->sin_addr, c->peer_ip, sizeof(c->peer_ip));
        }
    }
}

void connection_destroy(connection_t *c)
{
    if (c->file_fd >= 0) {
        close(c->file_fd);
        c->file_fd = -1;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c->recv_buf);
    c->recv_buf = NULL;
    c->recv_cap = 0;
    c->recv_len = 0;
}

int connection_feed(connection_t *c, const char *data, size_t len)
{
    if (c->recv_buf == NULL) {          /* 초기 할당 실패 */
        queue_error(c, 500, 0);
        c->closing = 1;
        return 0;
    }
    /* 받은 바이트를 누적한다. 초기 버퍼(8KB)로 부족하면 여기서 키운다. */
    if (len > c->recv_cap - c->recv_len) {
        size_t need = c->recv_len + len;
        size_t new_cap;
        char *nb;

        if (need > (size_t)CONN_RECV_MAX) {
            queue_error(c, 431, 0);
            c->closing = 1;
            c->recv_len = 0;
            return 0;
        }
        new_cap = c->recv_cap * 2;
        while (new_cap < need) new_cap *= 2;
        if (new_cap > (size_t)CONN_RECV_MAX) new_cap = (size_t)CONN_RECV_MAX;
        nb = realloc(c->recv_buf, new_cap);
        if (nb == NULL) {
            queue_error(c, 500, 0);
            c->closing = 1;
            c->recv_len = 0;
            return 0;
        }
        c->recv_buf = nb;
        c->recv_cap = new_cap;
    }
    if (len > 0) {
        memcpy(c->recv_buf + c->recv_len, data, len);
        c->recv_len += len;
    }
    connection_touch(c);

    for (;;) {
        http_request_t req;
        parse_status_t st = http_parser_parse(c->recv_buf, c->recv_len, &req);
        int keep_alive;
        int resp_status = 500;
        size_t off, n = 0;

        if (st == PARSE_NEED_MORE) {
            return 0;   /* 버퍼에 요청 일부만 있음 */
        }
        if (st == PARSE_ERROR) {
            queue_error(c, req.err_status, 0);
            c->closing = 1;
            c->recv_len = 0;
            return 0;
        }

        keep_alive = (req.http_minor == 1 && !req.connection_close);
        if (keep_alive && c->cfg != NULL &&
            c->requests + 1 >= c->cfg->keepalive_max) {
            keep_alive = 0;   /* 헤더도 close로 보내야 소켓과 일치 */
        }
        off = c->send_len;
        if (off >= sizeof(c->send_buf)) {
            c->closing = 1;
            c->recv_len = 0;
            return 0;
        }

        /* GET 외 메서드는 405. 정적 파일만 서빙한다. */
        {
            int is_get = (req.method_len == 3 && memcmp(req.method, "GET", 3) == 0);
            if (!is_get) {
                resp_status = 405;
                n = http_response_build_error(c->send_buf + off,
                                              sizeof(c->send_buf) - off,
                                              405, keep_alive);
            } else {
                static_file_t f = static_handler_resolve(req.uri, req.uri_len,
                                                         c->cfg->doc_root);
                resp_status = f.status;
                if (f.status == 200) {
                    n = http_response_build_file(c->send_buf + off,
                                                 sizeof(c->send_buf) - off,
                                                 f.mime, f.size, keep_alive, "");
                    if (n > 0) {
                        c->file_fd = f.file_fd;   /* 소유권 이전 */
                        c->file_off = 0;
                        c->file_size = f.size;
                    } else {
                        close(f.file_fd);
                    }
                } else {
                    n = http_response_build_error(c->send_buf + off,
                                                  sizeof(c->send_buf) - off,
                                                  f.status, keep_alive);
                }
            }
        }

        if (n == 0) {
            c->closing = 1;
            c->recv_len = 0;
            return 0;
        }
        c->send_len += n;

        {
            struct timespec done;
            long ms = 0;
            (void)clock_gettime(CLOCK_MONOTONIC, &done);
            ms = (long)((done.tv_sec - c->req_start.tv_sec) * 1000L +
                        (done.tv_nsec - c->req_start.tv_nsec) / 1000000L);
            c->status = resp_status;
            access_log(c, &req, ms);
            (void)clock_gettime(CLOCK_MONOTONIC, &c->req_start);
        }
        c->requests++;

        /* 파서가 알려준 소비 바이트만큼 제거(keep-alive pipelining 대비). */
        if (req.consumed >= c->recv_len) {
            c->recv_len = 0;
        } else {
            memmove(c->recv_buf, c->recv_buf + req.consumed,
                    c->recv_len - req.consumed);
            c->recv_len -= req.consumed;
        }

        if (!keep_alive) {
            c->closing = 1;
        }
        if (c->closing || c->recv_len == 0) {
            return 0;
        }
        /* 파일 본문을 보내는 중이면 다음 요청은 전송이 끝난 뒤 처리한다.
         * 여기서 이어 처리하면 다음 응답 헤더가 이 본문보다 먼저 send_buf에 붙고
         * file_fd도 덮어쓴다. 이어받기는 worker.c on_writable의
         * connection_feed(c, NULL, 0)가 맡는다. */
        if (c->file_fd >= 0) {
            return 0;
        }
        /* 남은 버퍼에 다음 요청이 완성되어 있으면 계속 처리 */
    }
}

int connection_flush(connection_t *c)
{
    while (c->send_off < c->send_len) {
        ssize_t n = send(c->fd, c->send_buf + c->send_off,
                         c->send_len - c->send_off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
            return -1;
        }
        c->send_off += (size_t)n;
    }
    c->send_len = 0;
    c->send_off = 0;

    /* 정적 파일 본문은 버퍼에 올리지 않고 청크 단위로 스트리밍. */
    while (c->file_fd >= 0 && c->file_off < c->file_size) {
        char chunk[4096];
        size_t want = sizeof(chunk);
        ssize_t r, n;

        if ((long)want > c->file_size - c->file_off) {
            want = (size_t)(c->file_size - c->file_off);
        }
        r = read(c->file_fd, chunk, want);
        if (r <= 0) break;

        n = send(c->fd, chunk, (size_t)r, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                memcpy(c->send_buf, chunk, (size_t)r);
                c->send_len = (size_t)r;
                c->send_off = 0;
                c->file_off += r;
                return 1;
            }
            return -1;
        }
        c->file_off += n;
        if (n < r) {
            memcpy(c->send_buf, chunk + n, (size_t)(r - n));
            c->send_len = (size_t)(r - n);
            c->send_off = 0;
            return 1;
        }
    }
    if (c->file_fd >= 0 && c->file_off >= c->file_size) {
        close(c->file_fd);
        c->file_fd = -1;
    }
    return 0;
}

int connection_want_write(const connection_t *c)
{
    return (c->send_off < c->send_len) || (c->file_fd >= 0);
}

void connection_touch(connection_t *c)
{
    c->last_active = time(NULL);
}

int connection_timed_out(const connection_t *c, long seconds)
{
    return (time(NULL) - c->last_active) > seconds;
}
