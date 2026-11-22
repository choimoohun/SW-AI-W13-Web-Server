/*
 * flight_clock.h — 출발/도착 추적용 시간源.
 *
 * real 모드: POSIX CLOCK_REALTIME.
 * sim  모드: 가상 시각 = 앵커 + (경과 실표초 × 배속). 출발 10분 전에서 시작하므로
 *           배속 3000이면 출발은 약 0.2초, 도착은 약 3초 뒤에 발생한다.
 */
#ifndef FLIGHT_CLOCK_H
#define FLIGHT_CLOCK_H

/* 현재 시각(UTC epoch 초). 스레드 안전. */
long flight_clock_now(void);

/* 운영 화면이 카운트다운을 맞출 수 있게 모드/배속/기준 시각을 알려준다. */
int  flight_clock_is_sim(void);
long flight_clock_speed(void);

#endif /* FLIGHT_CLOCK_H */
