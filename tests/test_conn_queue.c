/* test_conn_queue.c - conn_queue.c 단위 테스트 (docs/concurrency/test-cases.md) */
#undef NDEBUG
#include <assert.h>
#include <time.h>

#include "conn_queue.h"
#include "csapp.h"

static conn_queue q;

static void test_fifo(void) /* C01 */
{
    assert(conn_queue_init(&q) == 0);
    conn_queue_push(&q, 1);
    conn_queue_push(&q, 2);
    conn_queue_push(&q, 3);
    assert(conn_queue_pop(&q) == 1);
    assert(conn_queue_pop(&q) == 2);
    assert(conn_queue_pop(&q) == 3);
    conn_queue_destroy(&q);
}

static void test_close_drains(void) /* C02 */
{
    assert(conn_queue_init(&q) == 0);
    conn_queue_push(&q, 7);
    conn_queue_close(&q);
    assert(conn_queue_pop(&q) == 7); /* 닫혀도 남은 연결은 꺼냄 */
    assert(conn_queue_pop(&q) == -1);
    conn_queue_destroy(&q);
}

static void *pop_one(void *arg)
{
    *(int *)arg = conn_queue_pop(&q);
    return NULL;
}

static void test_close_wakes_waiter(void) /* C03 */
{
    pthread_t t;
    int got = 0;
    assert(conn_queue_init(&q) == 0);
    assert(pthread_create(&t, NULL, pop_one, &got) == 0);
    nanosleep(&(struct timespec){.tv_nsec = 100 * 1000 * 1000}, NULL); /* 워커가 빈 대기열에서 잠들 시간 */
    conn_queue_close(&q);
    assert(pthread_join(t, NULL) == 0);
    assert(got == -1);
    conn_queue_destroy(&q);
}

static void *push_full_plus_one(void *arg)
{
    (void)arg;
    for (int i = 0; i <= CONN_QUEUE_CAP; i++)
        conn_queue_push(&q, i);
    return NULL;
}

static void test_full_blocks_push(void) /* C04 */
{
    pthread_t t;
    assert(conn_queue_init(&q) == 0);
    assert(pthread_create(&t, NULL, push_full_plus_one, NULL) == 0);
    nanosleep(&(struct timespec){.tv_nsec = 100 * 1000 * 1000}, NULL);
    pthread_mutex_lock(&q.lock);
    assert(q.count == CONN_QUEUE_CAP); /* 마지막 push는 자리를 기다림 */
    pthread_mutex_unlock(&q.lock);
    assert(conn_queue_pop(&q) == 0);
    assert(pthread_join(t, NULL) == 0);
    assert(q.count == CONN_QUEUE_CAP);
    for (int i = 1; i <= CONN_QUEUE_CAP; i++)
        assert(conn_queue_pop(&q) == i);
    conn_queue_destroy(&q);
}

int main(void)
{
    test_fifo();
    test_close_drains();
    test_close_wakes_waiter();
    test_full_blocks_push();
    return 0;
}
