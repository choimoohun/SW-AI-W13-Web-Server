#ifndef BOOKING_H
#define BOOKING_H

#include <stddef.h>

struct booking_response {
    int status;
    const char *allow;
    char body[512];
};

void booking_request(const char *method, const char *path, const char *content_type,
                     const char *body, size_t length, struct booking_response *response);

#endif
