/*
 * iomux.c — I/O 멀티플렉싱 추상화.
 *
 * 플랫폼별 최적 백엔드를 같은 인터페이스 뒤에 둔다.
 *   Linux  : epoll  (관심 집합을 커널에 등록, 준비된 fd만 통지)
 *   macOS  : kqueue (epoll과 같은 역할, udata에 컨텍스트 포인터 저장)
 *   기타   : poll   (이식성 폴백, fd 배열을 매번 전달)
 *
 * 워커 스레드는 자기 iomux 인스턴스를 스레드 시작 후에 만든다(소유권 계약).
 * add/mod에 넣은 ptr은 wait의 event.ptr로 그대로 돌아온다.
 */
#include "iomux.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      /* close */

#if defined(__linux__)
#define IOMUX_BACKEND_EPOLL
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || \
      defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#define IOMUX_BACKEND_KQUEUE
#include <sys/event.h>
#include <sys/time.h>
#else
#define IOMUX_BACKEND_POLL
#endif

#define INITIAL_CAP 256

typedef struct {
    int fd;
    unsigned events;
    void *ptr;
} iomux_entry_t;

struct iomux {
    iomux_entry_t *entries;
    size_t n;
    size_t cap;

#if defined(IOMUX_BACKEND_POLL)
    struct pollfd *pfds;
#elif defined(IOMUX_BACKEND_EPOLL)
    int epfd;
    struct epoll_event *events;
#elif defined(IOMUX_BACKEND_KQUEUE)
    int kq;
    struct kevent *events;
#endif
};

/* ---------- 공통 유틸 ---------- */

static int find_fd(const iomux *m, int fd)
{
    for (size_t i = 0; i < m->n; ++i) {
        if (m->entries[i].fd == fd) return (int)i;
    }
    return -1;
}

#if defined(IOMUX_BACKEND_EPOLL)
static void *ptr_for_fd(const iomux *m, int fd)
{
    int i = find_fd(m, fd);
    return i < 0 ? NULL : m->entries[i].ptr;
}
#endif

static int grow(iomux *m)
{
    size_t new_cap = m->cap * 2;
    iomux_entry_t *e = realloc(m->entries, new_cap * sizeof(*e));

    if (e == NULL) return -1;
    m->entries = e;

#if defined(IOMUX_BACKEND_POLL)
    {
        struct pollfd *p = realloc(m->pfds, new_cap * sizeof(*p));
        if (p == NULL) return -1;   /* entries는 커졌지만 cap은 그대로 */
        m->pfds = p;
    }
#elif defined(IOMUX_BACKEND_EPOLL)
    {
        struct epoll_event *p = realloc(m->events, new_cap * sizeof(*p));
        if (p == NULL) return -1;
        m->events = p;
    }
#elif defined(IOMUX_BACKEND_KQUEUE)
    {
        struct kevent *p = realloc(m->events, new_cap * sizeof(*p));
        if (p == NULL) return -1;
        m->events = p;
    }
#endif
    m->cap = new_cap;
    return 0;
}

/* ---------- 백엔드별 등록/변경/제거 ---------- */

#if defined(IOMUX_BACKEND_EPOLL)
static uint32_t to_epoll_events(unsigned events)
{
    uint32_t e = 0;
    if (events & IOMUX_READ)  e |= EPOLLIN;
    if (events & IOMUX_WRITE) e |= EPOLLOUT;
    return e;
}
#endif

static int backend_add(iomux *m, const iomux_entry_t *e)
{
#if defined(IOMUX_BACKEND_EPOLL)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll_events(e->events);
    ev.data.fd = e->fd;
    return epoll_ctl(m->epfd, EPOLL_CTL_ADD, e->fd, &ev);
#elif defined(IOMUX_BACKEND_KQUEUE)
    struct kevent ch[2];
    int n = 0;
    if (e->events & IOMUX_READ) {
        EV_SET(&ch[n++], e->fd, EVFILT_READ, EV_ADD, 0, 0, e->ptr);
    }
    if (e->events & IOMUX_WRITE) {
        EV_SET(&ch[n++], e->fd, EVFILT_WRITE, EV_ADD, 0, 0, e->ptr);
    }
    if (n == 0) return 0;
    return kevent(m->kq, ch, (int)n, NULL, 0, NULL);
#else
    (void)m; (void)e;
    return 0;   /* poll은 등록 syscall 없음 */
#endif
}

static int backend_mod(iomux *m, const iomux_entry_t *old,
                       const iomux_entry_t *e)
{
#if defined(IOMUX_BACKEND_EPOLL)
    (void)old;   /* epoll_ctl(MOD)는 이전 상태가 필요 없음 */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll_events(e->events);
    ev.data.fd = e->fd;
    return epoll_ctl(m->epfd, EPOLL_CTL_MOD, e->fd, &ev);
#elif defined(IOMUX_BACKEND_KQUEUE)
    struct kevent ch[4];
    int n = 0;

    /* 빠지는 필터는 DEL, 새로 들어가는 필터는 ADD.
     * EV_DELETE은 이미 없으면 ENOENT를 주므로 무시한다. */
    if ((old->events & IOMUX_READ) && !(e->events & IOMUX_READ)) {
        EV_SET(&ch[n++], e->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if ((old->events & IOMUX_WRITE) && !(e->events & IOMUX_WRITE)) {
        EV_SET(&ch[n++], e->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    if ((e->events & IOMUX_READ) && !(old->events & IOMUX_READ)) {
        EV_SET(&ch[n++], e->fd, EVFILT_READ, EV_ADD, 0, 0, e->ptr);
    } else if (e->events & IOMUX_READ) {
        EV_SET(&ch[n++], e->fd, EVFILT_READ, EV_ADD, 0, 0, e->ptr);
    }
    if ((e->events & IOMUX_WRITE) && !(old->events & IOMUX_WRITE)) {
        EV_SET(&ch[n++], e->fd, EVFILT_WRITE, EV_ADD, 0, 0, e->ptr);
    } else if (e->events & IOMUX_WRITE) {
        EV_SET(&ch[n++], e->fd, EVFILT_WRITE, EV_ADD, 0, 0, e->ptr);
    }
    if (n == 0) return 0;
    return kevent(m->kq, ch, (int)n, NULL, 0, NULL);
#else
    (void)m; (void)old; (void)e;
    return 0;
#endif
}

static void backend_del(iomux *m, int fd, unsigned old_events)
{
#if defined(IOMUX_BACKEND_EPOLL)
    (void)old_events;   /* epoll_ctl(DEL)는 이전 상태가 필요 없음 */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    (void)epoll_ctl(m->epfd, EPOLL_CTL_DEL, fd, &ev);
#elif defined(IOMUX_BACKEND_KQUEUE)
    struct kevent ch[2];
    int n = 0;
    if (old_events & IOMUX_READ) {
        EV_SET(&ch[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (old_events & IOMUX_WRITE) {
        EV_SET(&ch[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    if (n > 0) (void)kevent(m->kq, ch, (int)n, NULL, 0, NULL);
#else
    (void)m; (void)fd; (void)old_events;
#endif
}

/* ---------- 공개 API ---------- */

iomux *iomux_create(void)
{
    iomux *m = calloc(1, sizeof(*m));

    if (m == NULL) return NULL;
    m->entries = calloc(INITIAL_CAP, sizeof(*m->entries));
    if (m->entries == NULL) { free(m); return NULL; }
    m->cap = INITIAL_CAP;
    m->n = 0;

#if defined(IOMUX_BACKEND_POLL)
    m->pfds = calloc(INITIAL_CAP, sizeof(*m->pfds));
    if (m->pfds == NULL) { free(m->entries); free(m); return NULL; }
#elif defined(IOMUX_BACKEND_EPOLL)
    m->epfd = epoll_create1(0);
    m->events = calloc(INITIAL_CAP, sizeof(*m->events));
    if (m->epfd < 0 || m->events == NULL) {
        if (m->epfd >= 0) close(m->epfd);
        free(m->events); free(m->entries); free(m); return NULL;
    }
#elif defined(IOMUX_BACKEND_KQUEUE)
    m->kq = kqueue();
    m->events = calloc(INITIAL_CAP, sizeof(*m->events));
    if (m->kq < 0 || m->events == NULL) {
        if (m->kq >= 0) close(m->kq);
        free(m->events); free(m->entries); free(m); return NULL;
    }
#endif
    return m;
}

void iomux_destroy(iomux *m)
{
    if (m == NULL) return;
#if defined(IOMUX_BACKEND_EPOLL)
    if (m->epfd >= 0) close(m->epfd);
    free(m->events);
#elif defined(IOMUX_BACKEND_KQUEUE)
    if (m->kq >= 0) close(m->kq);
    free(m->events);
#elif defined(IOMUX_BACKEND_POLL)
    free(m->pfds);
#endif
    free(m->entries);
    free(m);
}

int iomux_add(iomux *m, int fd, unsigned events, void *ptr)
{
    if (m == NULL || fd < 0 || find_fd(m, fd) >= 0) return -1;
    if (m->n == m->cap && grow(m) < 0) return -1;

    m->entries[m->n].fd = fd;
    m->entries[m->n].events = events;
    m->entries[m->n].ptr = ptr;
    if (backend_add(m, &m->entries[m->n]) < 0) {
        m->entries[m->n].fd = -1;
        return -1;
    }
    m->n++;
    return 0;
}

int iomux_mod(iomux *m, int fd, unsigned events, void *ptr)
{
    int i = find_fd(m, fd);
    iomux_entry_t next;

    if (i < 0) return -1;
    next = m->entries[i];
    next.events = events;
    next.ptr = ptr;
    if (backend_mod(m, &m->entries[i], &next) < 0) return -1;
    m->entries[i] = next;
    return 0;
}

int iomux_del(iomux *m, int fd)
{
    int i = find_fd(m, fd);

    if (i < 0) return -1;
    backend_del(m, fd, m->entries[i].events);
    if ((size_t)i != m->n - 1) {
        m->entries[i] = m->entries[m->n - 1];
    }
    m->n--;
    return 0;
}

int iomux_wait(iomux *m, iomux_event *out, int max, int timeout_ms)
{
    if (m == NULL || out == NULL || max <= 0) return -1;

#if defined(IOMUX_BACKEND_POLL)
    {
        int ready, count = 0;

        for (size_t i = 0; i < m->n; ++i) {
            m->pfds[i].fd = m->entries[i].fd;
            m->pfds[i].events = 0;
            m->pfds[i].revents = 0;
            if (m->entries[i].events & IOMUX_READ)  m->pfds[i].events |= POLLIN;
            if (m->entries[i].events & IOMUX_WRITE) m->pfds[i].events |= POLLOUT;
        }
        ready = poll(m->pfds, (nfds_t)m->n, timeout_ms);
        if (ready <= 0) return ready;

        for (size_t i = 0; i < m->n && count < max; ++i) {
            short r = m->pfds[i].revents;
            unsigned events = 0;
            if (r & (POLLIN | POLLHUP | POLLERR)) events |= IOMUX_READ;
            if (r & POLLOUT) events |= IOMUX_WRITE;
            if (events == 0) continue;
            out[count].fd = m->pfds[i].fd;
            out[count].events = events;
            out[count].ptr = m->entries[i].ptr;
            count++;
        }
        return count;
    }
#elif defined(IOMUX_BACKEND_EPOLL)
    {
        int want = max < (int)m->cap ? max : (int)m->cap;
        int n = epoll_wait(m->epfd, m->events, want, timeout_ms);
        if (n <= 0) return n;
        for (int i = 0; i < n; ++i) {
            unsigned events = 0;
            uint32_t e = m->events[i].events;
            if (e & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP)) events |= IOMUX_READ;
            if (e & EPOLLOUT) events |= IOMUX_WRITE;
            out[i].fd = m->events[i].data.fd;
            out[i].events = events;
            out[i].ptr = ptr_for_fd(m, out[i].fd);
        }
        return n;
    }
#elif defined(IOMUX_BACKEND_KQUEUE)
    {
        struct timespec ts;
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;

        int want = max < (int)m->cap ? max : (int)m->cap;
        int n = kevent(m->kq, NULL, 0, m->events, want, &ts);
        if (n <= 0) return n;
        for (int i = 0; i < n; ++i) {
            unsigned events = 0;
            if (m->events[i].filter == EVFILT_READ ||
                m->events[i].flags & EV_EOF ||
                m->events[i].flags & EV_ERROR) events |= IOMUX_READ;
            if (m->events[i].filter == EVFILT_WRITE) events |= IOMUX_WRITE;
            out[i].fd = (int)m->events[i].ident;
            out[i].events = events;
            out[i].ptr = m->events[i].udata;
        }
        return n;
    }
#endif
}
