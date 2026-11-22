/*
 * connection.h — 비동기 연결 컨텍스트. (STEP5: 논블로킹 상태 머신)
 *
 * [계약] 연결 하나의 수신/송신 상태를 구조체에 담고, feed/flush로 조금씩 진행한다.
 *   - connection_feed(): 받은 바이트 누적 → 파서 → 요청 완성 시 응답을 send_buf에 만든다.
 *   - connection_flush(): send_buf와 파일 본문을 nonblocking send로 밀어낸다.
 *   - keep-alive: 응답 후 컨텍스트를 재사용해 다음 요청을 받는다.
 *   - 정리 지점은 connection_destroy 하나. file_fd도 여기서 닫는다.
 */
#ifndef CONNECTION_H
#define CONNECTION_H

#include <stddef.h>   /* size_t */
#include <time.h>     /* time_t, struct timespec */

#include "config.h"

#define CONN_RECV_CAP 8192
#define CONN_SEND_CAP 17408   /* JSON 본문 16KB + 헤더 여유 */

typedef struct {
    int fd;
    const server_config_t *cfg;   /* 설정 소유자는 main */

    char recv_buf[CONN_RECV_CAP];
    size_t recv_len;

    char send_buf[CONN_SEND_CAP];
    size_t send_len;
    size_t send_off;

    int file_fd;               /* 파일 응답 시 열린 fd, 아니면 -1 */
    long file_off;
    long file_size;

    time_t last_active;
    struct timespec req_start; /* 요청 시작 시각(지연 측정) */
    char peer_ip[64];          /* 접근 로그용 클라이언트 IP */
    int worker_id;
    int requests;              /* 이 연결에서 처리한 요청 수(keep-alive 상한) */
    int status;                /* 최근 응답 상태코드(접근 로그) */
    int closing;               /* 응답 후 연결을 닫을지 (HTTP/1.0, Connection: close) */
    int proxied;               /* 1이면 프록시 스레드로 소유권 이전됨 → 워커가 연결 정리 */
} connection_t;

void connection_init(connection_t *c, int fd, const server_config_t *cfg,
                     int worker_id);
void connection_destroy(connection_t *c);

/* 수신 데이터를 누적하고 완성된 요청을 처리. 0=계속, -1=치명적 오류. */
int connection_feed(connection_t *c, const char *data, size_t len);

/* 0=다 보냄, 1=EAGAIN으로 다음 writable 대기, -1=오류. */
int connection_flush(connection_t *c);

int  connection_want_write(const connection_t *c);
void connection_touch(connection_t *c);
int  connection_timed_out(const connection_t *c, long seconds);

#endif /* CONNECTION_H */
