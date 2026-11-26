/*
 * main.c — 프로그램 진입점. 모듈 조립만 맡는다.
 *
 * 시그널 핸들러는 '플래그만' 세우고, 워커들이 g_stop을 보고 루프를 빠져나온 뒤
 * main이 join → listen fd 닫기 → 정상 종료 순으로 정리한다.
 */
#include "tcpserver.h"
#include "flight_store.h"
#include "worker.h"
#include "config.h"
#include "proxy.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WORKERS 16

/* 시그널 핸들러가 쓰고 워커 스레드가 읽는다. lock-free atomic_int는 핸들러 안에서도 안전하다.
 * (volatile sig_atomic_t는 신호에는 안전하지만 스레드 간에는 데이터 경쟁 — TSan 검출) */
static atomic_int g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    atomic_store(&g_stop, 1);
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

    flight_store_init();
    if (proxy_start() < 0) {
        fprintf(stderr, "proxy_start failed\n");
        return 1;
    }

    listen_fd = tcpserver_listen(port, 128);
    if (listen_fd < 0) { perror("tcpserver_listen"); proxy_stop(); return 1; }
    fprintf(stderr, "listening on port %s (doc_root=%s, workers=%d)\n",
            port, cfg.doc_root, cfg.workers);

    if (worker_pool_start(listen_fd, &cfg, &g_stop) < 0) {
        fprintf(stderr, "worker_pool_start failed\n");
        close(listen_fd);
        proxy_stop();
        return 1;
    }
    worker_pool_join();
    proxy_stop();

    close(listen_fd);
    return 0;
}
