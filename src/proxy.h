#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>
#include <stddef.h>

struct proxy_response {
    int status;
    char type[96];
    char allow[16];
    size_t length;
    char body[8192];
};

// 성공 시 0, 중계 실패 시 클라이언트에 보낼 502 또는 504를 반환한다.
int proxy_request(const struct sockaddr_in *backend, const char *method, const char *target,
                  const char *type, const char *body, size_t length,
                  struct proxy_response *response);

#endif
