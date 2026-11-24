#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <netinet/in.h>

struct http_config {
    const char *document_root;
    bool booking_backend;
    const char *proxy_prefix;
    struct sockaddr_in backend;
};

int handle_http_client(int connfd, const struct http_config *config);

#endif
