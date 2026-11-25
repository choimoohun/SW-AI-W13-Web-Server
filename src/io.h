#ifndef IO_H
#define IO_H

#include <stdint.h>

int set_nonblocking(int fd);
int64_t monotonic_ms(void);
int wait_fd(int fd, short events, int64_t deadline);

#endif
