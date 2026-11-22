/*
 * flight_clock.c — 실제/가상 시계 구현.
 *
 * 환경변수:
 *   FLIGHT_TIME_MODE=sim|real  (기본 real)
 *   FLIGHT_SIM_SPEED=<long>    (기본 600: 1실초 = 10가상분)
 */
#include "flight_clock.h"

#include <pthread.h>
#include <stdlib.h>   /* getenv, strtol */
#include <string.h>   /* strcmp */
#include <time.h>     /* clock_gettime */

#define SIM_ANCHOR_VIRTUAL 1795423800L   /* KE701 출발(1795424400) 10분 전 */

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int g_sim;
static long g_speed = 600;
static long g_real_epoch_ms;
static long g_virtual_epoch;

static long now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void clock_init(void)
{
    const char *mode = getenv("FLIGHT_TIME_MODE");
    const char *speed = getenv("FLIGHT_SIM_SPEED");

    g_sim = (mode != NULL && strcmp(mode, "sim") == 0);
    if (speed != NULL) {
        long v = strtol(speed, NULL, 10);
        if (v > 0) g_speed = v;
    }
    g_real_epoch_ms = now_ms();
    g_virtual_epoch = SIM_ANCHOR_VIRTUAL;
}

long flight_clock_now(void)
{
    (void)pthread_once(&g_once, clock_init);
    if (!g_sim) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
        return (long)ts.tv_sec;
    }
    return g_virtual_epoch + (now_ms() - g_real_epoch_ms) * g_speed / 1000L;
}

int flight_clock_is_sim(void)
{
    (void)pthread_once(&g_once, clock_init);
    return g_sim;
}

long flight_clock_speed(void)
{
    (void)pthread_once(&g_once, clock_init);
    return g_speed;
}
