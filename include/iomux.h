#ifndef IOMUX_H
#define IOMUX_H

/*
 * iomux — I/O 멀티플렉싱 추상화 모듈 (C-03 이식성 담당).
 *
 * [동작 원리]
 * 멀티플렉싱 = 하나의 실행 흐름이 여러 fd의 준비 상태를 커널에 한 번에 물어보는 것.
 *   poll  : POSIX 표준. fd 배열을 매 호출마다 전달 → 관심 fd 수에 비례(O(n)).
 *           macOS/Linux 모두 동작 → 이식성 기본안.
 *   epoll : Linux 전용. 관심 집합을 커널에 등록해 두고 변경분만 통지 → O(준비된 수).
 *   kqueue: macOS/BSD 전용. epoll과 같은 역할의 다른 API.
 * 셋 중 무엇을 쓰든 '워커별 이벤트 루프'라는 구조(D5)는 동일하다.
 *
 * [소유권/인스턴스 규칙]
 * iomux 인스턴스는 워커마다 '자기 것'을 만든다. 인스턴스 공유 금지.
 * (fork 기반이었다면 fork 전에 만든 epoll은 open file description이 공유되어
 *  전 워커가 같은 epoll을 쓰는 사고가 난다. 스레드 기반에서도 원칙은 동일:
 *  워커 스레드 시작 후에 각자 iomux_create.)
 *
 * [등록 포인터 계약]
 * iomux_add/mod의 ptr은 연결 컨텍스트 포인터를 넣는 자리다.
 * iomux_wait이 돌려주는 iomux_event.ptr로 그대로 돌아오므로,
 * 워커는 이벤트에서 곧바로 연결 상태를 얻는다.
 * (epoll 백엔드는 내부에서 fd로 등록 배열을 선형 탐색해 ptr을 찾는다. 이벤트당 O(N).
 *  STEP05 문서의 '알려진 한계' 참고.)
 */

/* 이벤트 준비 비트 */
#define IOMUX_READ  0x1u
#define IOMUX_WRITE 0x2u

typedef struct iomux iomux; /* 불투명 핸들. 내부는 backend별(epoll fd/kqueue fd/poll 배열) */

typedef struct {
    int      fd;     /* 준비된 fd */
    unsigned events; /* IOMUX_READ / IOMUX_WRITE 비트 */
    void    *ptr;    /* 등록 시 넣은 연결 컨텍스트 포인터 */
} iomux_event;

/* 인스턴스 생성. 실패 시 NULL. 워커별로 1개씩. */
iomux *iomux_create(void);

/* 인스턴스 해제. 등록된 fd는 닫지 않는다(fd 소유자는 워커/연결). */
void iomux_destroy(iomux *m);

/* fd를 관심 집합에 등록. events는 IOMUX_READ/WRITE 조합. 성공 0, 실패 -1. */
int iomux_add(iomux *m, int fd, unsigned events, void *ptr);

/* 관심 이벤트 변경(예: 읽기 완료 후 쓰기 대기 전환). 성공 0, 실패 -1. */
int iomux_mod(iomux *m, int fd, unsigned events, void *ptr);

/* 관심 집합에서 제거. fd 자체는 닫지 않는다. close 전에 반드시 del. */
int iomux_del(iomux *m, int fd);

/* 준비된 이벤트를 out에 최대 max개 채움. 반환 = 채운 개수, 0 = 타임아웃, -1 = 오류.
 * timeout_ms는 유휴 연결 정리/종료 플래그 확인 주기용(예: 100~500ms).
 * EINTR은 내부에서 재시도하거나 -1로 올리고 워커가 stop 플래그를 확인하게 한다. */
int iomux_wait(iomux *m, iomux_event *out, int max, int timeout_ms);

#endif /* IOMUX_H */
