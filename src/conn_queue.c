/*
 * conn_queue.c - 연결 소켓 대기열 (CS:APP3e 12.5.5의 sbuf를 뮤텍스와 조건 변수로)
 * 설계: docs/concurrency/
 */
#include "conn_queue.h"

int conn_queue_init(conn_queue *q)
{
    q->head = q->count = q->closed = 0;
    if (pthread_mutex_init(&q->lock, NULL) != 0)
        return -1;
    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        return -1;
    }
    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->lock);
        return -1;
    }
    return 0;
}

void conn_queue_push(conn_queue *q, int fd)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == CONN_QUEUE_CAP)
        pthread_cond_wait(&q->not_full, &q->lock);
    q->fds[(q->head + q->count) % CONN_QUEUE_CAP] = fd;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

int conn_queue_pop(conn_queue *q)
{
    int fd = -1;
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->lock);
    if (q->count > 0) {
        fd = q->fds[q->head];
        q->head = (q->head + 1) % CONN_QUEUE_CAP;
        q->count--;
        pthread_cond_signal(&q->not_full);
    }
    pthread_mutex_unlock(&q->lock);
    return fd;
}

void conn_queue_close(conn_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

void conn_queue_destroy(conn_queue *q)
{
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);
}
