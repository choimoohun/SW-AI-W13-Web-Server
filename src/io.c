#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <time.h>

int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int wait_fd(int fd, short events, int64_t deadline)
{
    struct pollfd descriptor = { .fd = fd, .events = events };
    for (;;) {
        int64_t now = monotonic_ms();
        if (now < 0) return -1;
        int64_t remaining = deadline - now;
        if (remaining <= 0) break;

        int ready = poll(&descriptor, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
        if (ready > 0) {
            if (!(descriptor.revents & POLLNVAL)) return 0;
            errno = EBADF;
            return -1;
        }
        if (ready < 0 && errno != EINTR) return -1;
    }
    errno = ETIMEDOUT;
    return -1;
}
