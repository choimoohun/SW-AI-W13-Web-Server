/*
 * test_flight_race.c — 좌석 경쟁 단위 테스트 (스레드 32개, KE999 1석).
 *
 * before 모드에서 초과 예약이 재현되고, after 모드에서 정확히 1건만 성공하는지 확인.
 * HTTP 서버 없이 store 계층만 검증하므로 STEP5 배선과 무관하게 동작한다.
 * macOS는 pthread_barrier가 공개되지 않아 volatile 플래그 스핀으로 출발선을 맞춘다.
 * 빌드/실행: make test-flight
 */
#include "flight_store.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NTHREADS 32

typedef struct {
    volatile int *go;
    int successes;
} worker_arg_t;

static void *reserve_worker(void *arg)
{
    worker_arg_t *a = (worker_arg_t *)arg;
    char out[32];

    while (*(a->go) == 0) { /* 모든 스레드가 만들어질 때까지 대기 */ }
    if (flight_store_reserve("KE999", 5, "user", 4, "", 0, "race-test", 9,
                                 out, sizeof(out)) == FS_OK) {
        a->successes++;
    }
    return NULL;
}

static int run_mode(int lock_enabled, const char *label)
{
    worker_arg_t args[NTHREADS];
    pthread_t tids[NTHREADS];
    volatile int go = 0;
    flight_t flights[4];
    int total = 0;
    size_t n;

    flight_store_init();
    flight_store_set_lock_enabled(lock_enabled);
    memset(args, 0, sizeof(args));

    for (int i = 0; i < NTHREADS; ++i) {
        args[i].go = &go;
        if (pthread_create(&tids[i], NULL, reserve_worker, &args[i]) != 0) {
            fprintf(stderr, "FAIL %s: pthread_create 실패\n", label);
            return -1;
        }
    }
    go = 1;   /* 동시에 출발 */
    for (int i = 0; i < NTHREADS; ++i) {
        (void)pthread_join(tids[i], NULL);
        total += args[i].successes;
    }

    n = flight_store_list(flights, 4);
    printf("[%s] 성공 %d건 / 요청 %d건", label, total, NTHREADS);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(flights[i].id, "KE999") == 0) {
            printf(" / 잔여 좌석 %d", flights[i].seat_left);
        }
    }
    printf("\n");
    return total;
}

int main(void)
{
    int after = run_mode(1, "after");
    int before = run_mode(0, "before");
    int fail = 0;

    if (after != 1) {
        printf("FAIL after: 기대 1건, 실제 %d건\n", after);
        fail = 1;
    }
    if (before <= 1) {
        printf("FAIL before: 초과 예약이 재현되지 않음 (%d건)\n", before);
        fail = 1;
    }
    if (fail == 0) {
        printf("test_flight_race: all passed\n");
    }
    return fail;
}
