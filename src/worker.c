/*
 * worker.c — 워커 스레드별 이벤트 루프 (STEP5 구현).
 *
 * 프로세스 1개 + 워커 N개. 각 워커가 자기 poll 인스턴스를 스레드 시작 후에 만들고,
 * 공유 listen fd를 nonblocking accept한다. 자기가 accept한 연결은 끝까지 소유한다.
 * 워커 간 fd 핸드오프도 큐도 없다. 공유 상태(항공편 좌석)만 pthread_mutex로 보호한다.
 */
#include "worker.h"

#include "iomux.h"
#include "connection.h"
#include "flight_store.h"
#include "flight_clock.h"

#include <errno.h>
#include <fcntl.h>       /* fcntl, O_NONBLOCK */
#include <pthread.h>
#include <stdlib.h>     /* calloc, free */
#include <sys/socket.h> /* accept */
#include <unistd.h>     /* close */

#define MAX_WORKERS   16
#define MAX_CONNS     128
#define MAX_EVENTS    256
#define IO_TIMEOUT_MS 200
#define IDLE_SECONDS  5

typedef struct {
    int id;
    int listen_fd;
    const char *doc_root;
    const server_config_t *cfg;
    volatile sig_atomic_t *stop;
    iomux *mux;
    connection_t conns[MAX_CONNS];
    int used[MAX_CONNS];
} worker_ctx_t;

static worker_ctx_t *g_workers[MAX_WORKERS];
static pthread_t g_threads[MAX_WORKERS];
static int g_nworkers;

static void close_conn(worker_ctx_t *w, connection_t *c)
{
    iomux_del(w->mux, c->fd);
    connection_destroy(c);
    for (int i = 0; i < MAX_CONNS; ++i) {
        if (&w->conns[i] == c) {
            w->used[i] = 0;
            break;
        }
    }
}

static void accept_ready(worker_ctx_t *w)
{
    /* 한 이벤트당 연결 하나만 accept한다.
     * 전부 받아버리면 첫 워커가 100개를 독점해 같은 스레드에서 직렬 처리되고,
     * 공유 좌석 경쟁이 사라진다(STEP5 accept 분산의 실제 함정). */
    int fd;
    int idx = -1;
    int flags;
    connection_t *c;

    do {
        fd = accept(w->listen_fd, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return;   /* EAGAIN 또는 오류 */

    flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        for (int i = 0; i < MAX_CONNS; ++i) {
            if (!w->used[i]) { idx = i; break; }
        }
    if (idx < 0) {
        close(fd);   /* 워커 연결 슬롯 포화 */
        return;
    }
        c = &w->conns[idx];
    connection_init(c, fd, w->cfg, w->id);
    w->used[idx] = 1;
    if (iomux_add(w->mux, fd, IOMUX_READ, c) < 0) {
        close_conn(w, c);
    }
}

static void on_readable(worker_ctx_t *w, connection_t *c)
{
    char buf[CONN_RECV_CAP];

    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof(buf), 0);
        if (n > 0) {
            connection_feed(c, buf, (size_t)n);
            if (c->proxied) { close_conn(w, c); return; }
            /* 응답 대기 중인데 더 읽으면 send_buf가 여러 응답으로 차므로 중단. */
            if (connection_want_write(c)) break;
            continue;
        }
        if (n == 0) { close_conn(w, c); return; }   /* 클라이언트 EOF */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        close_conn(w, c);
        return;
    }
    if (c->fd < 0) return;
    if (connection_want_write(c)) {
        (void)iomux_mod(w->mux, c->fd, IOMUX_WRITE, c);
    }
}

static void on_writable(worker_ctx_t *w, connection_t *c)
{
    int rc = connection_flush(c);
    if (rc < 0) {
        close_conn(w, c);
        return;
    }
    if (rc == 1) return;   /* 아직 EAGAIN → WRITE 관심 유지 */
    if (c->closing) {
        close_conn(w, c);
        return;
    }
    /* 소켓에는 새 데이터가 없지만 recv_buf에 다음 요청이 남아있을 수 있다. */
    connection_feed(c, NULL, 0);
    if (c->proxied) { close_conn(w, c); return; }
    if (c->closing) {
        close_conn(w, c);
        return;
    }
    if (connection_want_write(c)) return;   /* 새 응답 생성됨 → WRITE 유지 */
    (void)iomux_mod(w->mux, c->fd, IOMUX_READ, c);
}

static void cleanup_idle(worker_ctx_t *w)
{
    for (int i = 0; i < MAX_CONNS; ++i) {
        if (w->used[i] && connection_timed_out(&w->conns[i], IDLE_SECONDS)) {
            close_conn(w, &w->conns[i]);   /* 누적 버퍼가 있어도 5초면 정리 */
        }
    }
}

static void *worker_thread_main(void *arg)
{
    worker_ctx_t *w = (worker_ctx_t *)arg;
    iomux_event events[MAX_EVENTS];

    w->mux = iomux_create();
    if (w->mux == NULL) return NULL;
    if (iomux_add(w->mux, w->listen_fd, IOMUX_READ, w) < 0) {
        iomux_destroy(w->mux);
        w->mux = NULL;
        return NULL;
    }

    while (!*(w->stop)) {
        int n = iomux_wait(w->mux, events, MAX_EVENTS, IO_TIMEOUT_MS);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].ptr == w) {
                accept_ready(w);
            } else {
                connection_t *c = (connection_t *)events[i].ptr;
                if (c == NULL || c->fd != events[i].fd) continue;
                if (events[i].events & IOMUX_READ)  on_readable(w, c);
                if (c->fd < 0) continue;   /* 읽기에서 닫힘 */
                if (events[i].events & IOMUX_WRITE) on_writable(w, c);
            }
        }
        cleanup_idle(w);

        /* 자동 출발/도착 전이는 워커 0번에서만 구동한다.
         * 모든 워커가 호출하면 같은 이벤트가 N번 기록된다. */
        if (w->id == 0) {
            (void)flight_store_tick(flight_clock_now());
        }
    }

    for (int i = 0; i < MAX_CONNS; ++i) {
        if (w->used[i]) close_conn(w, &w->conns[i]);
    }
    iomux_destroy(w->mux);
    w->mux = NULL;
    return NULL;
}

int worker_pool_start(int listen_fd, const server_config_t *cfg,
                      volatile sig_atomic_t *stop)
{
    int flags;
    int started = 0;
    int nworkers = cfg->workers;

    if (nworkers < 1 || nworkers > MAX_WORKERS || listen_fd < 0) return -1;

    /* 공유 listen fd는 nonblocking. 워커 전원이 같은 fd에서 accept 경쟁. */
    flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    for (int i = 0; i < nworkers; ++i) {
        worker_ctx_t *w = calloc(1, sizeof(*w));
        if (w == NULL) goto fail;
        w->id = i;
        w->listen_fd = listen_fd;
        w->doc_root = cfg->doc_root;
        w->cfg = cfg;
        w->stop = stop;
        g_workers[i] = w;

        if (pthread_create(&g_threads[i], NULL, worker_thread_main, w) != 0) {
            free(w);
            g_workers[i] = NULL;
            goto fail;
        }
        started++;
    }
    g_nworkers = nworkers;
    return 0;

fail:
    *stop = 1;   /* 이미 만든 워커들을 종료시켜 회수 */
    for (int i = 0; i < started; ++i) {
        (void)pthread_join(g_threads[i], NULL);
        free(g_workers[i]);
        g_workers[i] = NULL;
    }
    g_nworkers = 0;
    return -1;
}

void worker_pool_join(void)
{
    for (int i = 0; i < g_nworkers; ++i) {
        (void)pthread_join(g_threads[i], NULL);
        free(g_workers[i]);
        g_workers[i] = NULL;
    }
    g_nworkers = 0;
}
