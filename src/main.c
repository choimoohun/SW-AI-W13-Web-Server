/*
 * main.c — 프로그램 진입점. 모듈 조립만 맡는다. (오늘 범위: 정적 파일까지)
 * 원리: 시그널 핸들러는 '플래그만' 세우고, 실제 정리는 루프가 빠져나온 뒤 main에서 수행.
 *
 * 마일스톤3~4: accept 루프에서 connection_handle(client_fd, doc_root) 호출.
 * 확인: make → ./webserver → curl http://127.0.0.1:8080/ (index.html).
 */

#include "tcpserver.h"
#include "connection.h"
#include <signal.h>   /* signal, sigaction, SIGPIPE, SIGINT, SIGTERM, sig_atomic_t */
#include <stdio.h>    /* perror */
#include <string.h>   /* memset */
#include <unistd.h>   /* close */

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;   /* 플래그만 세움 → accept 루프가 이걸 보고 빠져나옴 */
}

int main(void)
{
    /* 1) 시그널 정책 (NFR-02). SIGPIPE 무시(전송 중 클라이언트 끊김에도 생존). */
    signal(SIGPIPE, SIG_IGN);

    /*    SIGINT/SIGTERM: g_stop 만 세움. SA_RESTART 끔 → accept 가 EINTR 로中断 → 루프 탈출. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* 2) 설정: 오늘은 고정. TODO(나중): conf/CLI(-p/-r)에서 port·doc_root (C-05). */
    const char *port = "8080";
    const char *doc_root = "./www";

    /* 3) listen 소켓 열기 (FR-01). */
    int listen_fd = tcpserver_listen(port, 128);
    if (listen_fd < 0) { perror("tcpserver_listen"); return 1; }

    /* 4) accept 루프 — 종료 플래그가 설 때까지. */
    while (!g_stop) {
        int client_fd = tcpserver_accept(listen_fd);
        if (client_fd < 0) {
            if (g_stop) break;
            continue;
        }
        connection_handle(client_fd, doc_root);   /* 읽기/파싱/정적파일/응답/닫기 */
    }

    /* 5) 정리: listen fd 닫고 정상 종료 (exit 0). */
    close(listen_fd);
    return 0;
}
