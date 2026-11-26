/*
 * test_flight_tracking.c — 출발/도착 추적 단위 검증.
 *  1) sim 시계가 배속만큼 흐르는지
 *  2) tick이 경계에서 정확히 한 번만 전이하는지(멱등)
 *  3) 지연 입력이 예정 시각을 밀고 조기 출발을 막는지
 * 빌드/실행: make test-tracking
 */
#include "flight_clock.h"
#include "flight_store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>   /* sleep 등 */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static int find_flight(const char *id)
{
    flight_t flights[8];
    size_t n = flight_store_list(flights, 8);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(flights[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static flight_t get_flight(const char *id)
{
    flight_t flights[8];
    size_t n = flight_store_list(flights, 8);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(flights[i].id, id) == 0) return flights[i];
    }
    memset(flights, 0, sizeof(flights));
    return flights[0];
}

static int event_count(const char *id, flight_status_t status)
{
    flight_event_t ev[32];
    size_t n = flight_store_timeline(id, strlen(id), ev, 32);
    int count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (ev[i].status == status) count++;
    }
    return count;
}

int main(void)
{
    long t0, t1;
    flight_t f;

    /* 1) sim 시계 배속 */
    t0 = flight_clock_now();
    {
        struct timespec nap;
        nap.tv_sec = 0; nap.tv_nsec = 200000L * 1000L;
        (void)nanosleep(&nap, NULL);   /* 0.2초 */
    }
    t1 = flight_clock_now();
    printf("[clock] 0.2초 경과 -> 가상 %ld초 진행 (mode=%d, speed=%ld)\n",
           t1 - t0, flight_clock_is_sim(), flight_clock_speed());
    CHECK(flight_clock_is_sim() == 1, "sim 모드여야 함");
    CHECK(t1 - t0 >= 100, "배속 1000이면 0.2초에 200초 이상 진행");

    /* 2) tick 경계/멱등 */
    flight_store_init();
    CHECK(find_flight("KE701") >= 0, "KE701 시드 존재");
    f = get_flight("KE701");
    CHECK(f.status == F_SCHEDULED, "초기 상태 SCHEDULED");
    /* 다른 편은 이미 출발 시각이 지나 있을 수 있으니 전체 반환값 대신 KE701 상태만 본다. */
    (void)flight_store_tick(f.depart_epoch - 1);
    f = get_flight("KE701");
    CHECK(f.status == F_SCHEDULED, "출발 직전에는 KE701 전이 없음");
    CHECK(flight_store_tick(f.depart_epoch) > 0, "출발 시각에 전이 발생");
    f = get_flight("KE701");
    CHECK(f.status == F_DEPARTED, "출발 상태");
    CHECK(f.actual_depart_epoch == f.depart_epoch, "실제 출발 시각 기록");
    CHECK(flight_store_tick(f.depart_epoch + 1) == 0, "같은 전이 중복 없음");
    CHECK(event_count("KE701", F_DEPARTED) == 1, "출발 이벤트 1건");
    CHECK(flight_store_tick(f.arrive_epoch) > 0, "도착 시각에 전이 발생");
    f = get_flight("KE701");
    CHECK(f.status == F_ARRIVED, "도착 상태");
    CHECK(f.actual_arrive_epoch == f.arrive_epoch, "실제 도착 시각 기록");
    CHECK(event_count("KE701", F_ARRIVED) == 1, "도착 이벤트 1건");

    /* 3) 지연 입력 */
    flight_store_init();
    f = get_flight("KE202");
    CHECK(flight_store_delay("KE202", 5, 30, 100) == FS_OK, "지연 30분 적용");
    f = get_flight("KE202");
    CHECK(f.status == F_DELAYED, "지연 상태");
    CHECK(f.depart_epoch == 1795507200L + 1800L, "출발 30분 밀림");
    CHECK(f.arrive_epoch == 1795514400L + 1800L, "도착 30분 밀림");
    (void)flight_store_tick(1795507200L + 60L);
    f = get_flight("KE202");
    CHECK(f.status == F_DELAYED, "지연 전에는 KE202가 출발하지 않음");
    (void)flight_store_tick(f.depart_epoch);
    f = get_flight("KE202");
    CHECK(f.status == F_DEPARTED, "밀린 시각에 KE202 출발");
    CHECK(event_count("KE202", F_DELAYED) == 1, "지연 이벤트 1건");

    if (failures == 0) printf("test_flight_tracking: all passed\n");
    return failures;
}
