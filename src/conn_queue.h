#ifndef CONN_QUEUE_H
#define CONN_QUEUE_H

#include <pthread.h>

#define CONN_QUEUE_CAP 16

/* main이 넣고 워커가 꺼내는 연결 소켓 대기열. 원형 버퍼를 뮤텍스 하나와 조건 변수 둘로 보호합니다. */
typedef struct {
    int fds[CONN_QUEUE_CAP];
    int head;   /* 다음에 꺼낼 위치 */
    int count;  /* 들어 있는 개수 */
    int closed; /* conn_queue_close() 이후 1 */
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} conn_queue;

/*
 * q를 빈 대기열로 초기화합니다. 성공하면 0, 뮤텍스나 조건 변수 초기화에 실패하면 -1을 반환합니다.
 * 성공했으면 다 쓴 뒤 conn_queue_destroy()로 정리해야 합니다.
 */
int conn_queue_init(conn_queue *q);

/*
 * fd를 대기열 끝에 넣습니다. 가득 차 있으면 자리가 날 때까지 기다립니다. 반환값은 없습니다.
 * conn_queue_close() 뒤에는 호출하지 않습니다. fd의 소유권은 꺼내는 워커에게 넘어갑니다.
 */
void conn_queue_push(conn_queue *q, int fd);

/*
 * 대기열 앞의 fd를 꺼내 반환합니다. 비어 있으면 들어올 때까지 기다립니다.
 * 닫힌 대기열이 비면 -1을 반환합니다. 꺼낸 fd는 호출자가 닫아야 합니다.
 */
int conn_queue_pop(conn_queue *q);

/*
 * 대기열을 닫고 기다리는 워커를 모두 깨웁니다. 남은 fd는 계속 꺼낼 수 있습니다. 반환값은 없습니다.
 */
void conn_queue_close(conn_queue *q);

/*
 * 뮤텍스와 조건 변수를 정리합니다. 기다리는 스레드가 없을 때 호출합니다. 남은 fd는 닫지 않습니다.
 */
void conn_queue_destroy(conn_queue *q);

#endif /* CONN_QUEUE_H */
