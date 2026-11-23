/*
 * main.c — 프로그램 진입점. 모듈 조립만 맡는다.
 *
 * 시그널 핸들러는 '플래그만' 세우고, 워커들이 g_stop을 보고 루프를 빠져나온 뒤
 * main이 join → listen fd 닫기 → 정상 종료 순으로 정리한다.
 */
#include "tcpserver.h"
#include "worker.h"
#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WORKERS 16

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(int argc, char **argv)
{
    server_config_t cfg;
    const char *port;
    const char *env;
    int listen_fd;

    (void)config_load(argc, argv, &cfg);
    port = cfg.port;

    /* 환경변수는 conf/CLI 다음의 마지막 오버라이드(테스트 포트 격리용). */
    env = getenv("WEB_PORT");
    if (env != NULL && env[0] != '\0') port = env;
    env = getenv("WEB_WORKERS");
    if (env != NULL) {
        long n = strtol(env, NULL, 10);
        if (n >= 1 && n <= MAX_WORKERS) cfg.workers = (int)n;
    }

    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    listen_fd = tcpserver_listen(port, 128);
    if (listen_fd < 0) { perror("tcpserver_listen"); return 1; }
    fprintf(stderr, "listening on port %s (doc_root=%s, workers=%d)\n",
            port, cfg.doc_root, cfg.workers);

    if (worker_pool_start(listen_fd, &cfg, &g_stop) < 0) {
        fprintf(stderr, "worker_pool_start failed\n");
        close(listen_fd);
        return 1;
    }
    worker_pool_join();

    close(listen_fd);
    return 0;
}
