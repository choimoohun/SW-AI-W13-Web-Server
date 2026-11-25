#ifndef WORKERS_H
#define WORKERS_H

#include "http.h"

// Takes ownership of listenfd and closes it on every exit path.
int serve_connections(int listenfd, const struct http_config *config);

#endif
