#ifndef WORKER_H
#define WORKER_H

/*
 * worker — Envoy식 multi-reactor 동시성 모듈 (D5, FR-06).
 *
 * [구조 — 발표용]
 * 프로세스 1개 + 워커 스레드 N개. 각 스레드가 '자기 iomux 이벤트 루프'를 돌린다.
 *   영상 패턴(a): epoll 1개 + 큐 + 스레드풀 → fd 핸드오프 규칙 필요.
 *   nginx 패턴(b): 워커 프로세스 N개 각자 epoll → 공유 상태에 IPC 필요.
 *   이 모듈(c)  : 워커 스레드 N개 각자 epoll/poll → 핸드오프도 IPC도 없음.
 * 동시성 = N 워커 × 각자 수백 fd 멀티플렉싱. 큐는 없다. 루프가 곧 처리자.
 *
 * [소유권 계약]
 * 자기가 accept한 연결은 그 워커가 끝까지 소유(읽기/파싱/응답/닫기).
 * 워커 간 fd 전달 없음 → 핫패스에 락 없음.
 * listen fd는 main이 소유(생성/닫기). 워커는 등록만 하고 닫지 않는다.
 *
 * [공유 상태]
 * 같은 주소 공간이므로 좌석 등 전역 데이터는 pthread_mutex로 보호하면 된다.
 * (항공편 예약 before/after 데모: 락 없음=초과 예약, 락 있음=정확히 1건 성공.)
 *
 * [accept 분산]
 * 기본안(양 OS): 공유 listen fd를 전 워커가 각자 루프에 등록 + nonblocking accept.
 *   깨어난 워커 중 하나만 accept 성공, 나머지는 EAGAIN/EWOULDBLOCK → 무시.
 *   데모 규모(동시 100+)에서 thundering herd는 허용 가능.
 * Linux 선택 최적화: EPOLLEXCLUSIVE 또는 워커별 SO_REUSEPORT(커널이 연결 분산).
 *   macOS의 SO_REUSEPORT 분산 semantics는 미검증 → 기본안 유지(C-03).
 *
 * [어피니티]
 * Linux에서만 pthread_setaffinity_np로 워커 스레드를 코어에 고정(선택, 성능용).
 * macOS은 코어 고정 API 없음 → no-op. 정확성과 무관.
 *
 * [종료 — NFR-02]
 * main의 g_stop(volatile sig_atomic_t)을 전 워커가 공유.
 * iomux_wait 타임아웃 주기로 플래그를 확인 → 루프 탈출 → 자기 연결 정리 → 스레드 종료.
 * main은 worker_pool_join으로 전 워커 회수 후 listen fd 닫고 exit 0.
 *
 * 아래 선언은 제안 인터페이스다. 구현하며 시그니처를 조정해도 된다.
 */

#include <signal.h> /* sig_atomic_t */
#include "config.h"

/* 워커 N개 기동. 성공 0, 실패 -1.
 * listen_fd는 호출 전에 nonblocking이어야 하며 닫기는 main 소유.
 * stop은 main의 종료 플래그 주소(워커는 읽기만). */
int worker_pool_start(int listen_fd, const server_config_t *cfg,
                      volatile sig_atomic_t *stop);

/* 전 워커 종료 대기(pthread_join). worker_pool_start 성공 후에만 호출. */
void worker_pool_join(void);

#endif /* WORKER_H */
