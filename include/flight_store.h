/*
 * flight_store.h — 항공편 인메모리 저장소 인터페이스. (STEP6 스캐폴딩)
 *
 * [역할] 항공편/예약/타임라인을 메모리에 들고 있는 유일한 곳.
 *        HTTP나 JSON을 모른다. flight_api가 이 함수들을 호출해 응답을 만든다.
 *
 * [동시성 계약]
 *   - 워커 스레드 N개가 같은 저장소를 동시에 접근한다(STEP5 전제).
 *   - after 모드(lock_enabled=1): 항공편별 락(flight_t.mu)으로 좌석/상태를 보호하고,
 *     g_store.mu는 예약 목록·모드 같은 메타데이터만 보호한다. 파일 저장은 스냅샷 후
 *     락 밖에서 한다.
 *   - before 모드(lock_enabled=0): reserve 경로만 의도적으로 락을 빼서
 *     초과 예약을 재현한다. 데모용 UB이며 절대 기본값으로 두지 않는다.
 *   - 락 안에서는 값을 지역 버퍼로 복사만 하고, 복사한 뒤 unlock하고
 *     JSON 직렬화/전송은 락 밖에서 한다(락 보유 시간 최소화).
 *
 * [메모리] 고정 배열만 쓴다. malloc 없음 → ASan/valgrind 누수 추적이 단순해진다.
 */
#ifndef FLIGHT_STORE_H
#define FLIGHT_STORE_H

#include <pthread.h>   /* pthread_mutex_t: 구조체에 락을 담기 위해 */
#include <stddef.h>    /* size_t */

/* 상태 값. F_INVALID는 "문자열 → 열거" 변환 실패(HTTP 400)용 센티넬. */
typedef enum {
    F_INVALID = -1,
    F_SCHEDULED = 0,
    F_BOARDING,
    F_DEPARTED,
    F_DELAYED,
    F_ARRIVED
} flight_status_t;

typedef struct {
    char id[16];               /* "KE701" */
    char from[8], to[8];       /* "ICN", "NRT" */
    long depart_epoch;         /* 예정 출발 (UTC 초) */
    long arrive_epoch;         /* 예정 도착 (UTC 초) */
    long actual_depart_epoch;  /* 실제 출발, 0이면 아직 안 함 */
    long actual_arrive_epoch;  /* 실제 도착, 0이면 아직 안 함 */
    long delay_applied_min;    /* 운영자가 누적 적용한 지연(분). 예정 시각에 반영됨 */
    int  seat_total;
    int  seat_left;            /* ← 예약 경쟁의 대상. 음수로 내려가면 안 됨 */
    flight_status_t status;
    pthread_mutex_t mu;        /* 이 항공편의 좌석/상태/타임라인 전용 락 */
} flight_t;

typedef struct {
    char id[32];               /* "RS-0001" 같은 증가번호 */
    char flight_id[16];
    char name[64];             /* 승객 이름(UTF-8) */
    char contact[64];          /* 연락처/이메일 */
    char client_id[64];        /* 브라우저가 발급한 사용자 식별자 */
    long created_epoch;
    int  cancelled;            /* 1=취소됨(좌석 복원됨) */
} reservation_t;

typedef struct {
    long ts;                   /* 이벤트 발생 시각 */
    flight_status_t status;
    char note[128];            /* "지연: 기상 악화" 등 */
} flight_event_t;

typedef struct {
    flight_t flights[16];
    size_t flight_count;
    reservation_t reservations[256];
    size_t reservation_count;
    flight_event_t events[16][32];  /* 항공편 인덱스별 타임라인 */
    size_t event_count[16];
    pthread_mutex_t mu;
    int lock_enabled;          /* 1=after(기본), 0=before(데모용 레이스) */
} flight_store_t;

/* 구현 함수가 돌려주는 결과. HTTP 상태코드는 flight_api에서 매핑한다. */
typedef enum {
    FS_OK = 0,
    FS_NOT_FOUND,
    FS_SOLD_OUT,
    FS_BAD_REQUEST
} flight_result_t;

/* 저장 파일 경로 지정. init 전에 호출. 기본 "data/flights.txt",
 * FLIGHT_DATA_FILE 환경변수로도 지정 가능. "/dev/null"이면 비영속 모드. */
void flight_store_set_data_path(const char *path);

/* 서버 시작 시 한 번 호출: 파일 로드(없으면 시드) + 뮤텍스 초기화 + after 모드. */
void flight_store_init(void);

/* 스토어 스냅샷을 out에 복사(최대 cap개). 복사한 개수 반환.
 * 내부 포인터를 직접 돌려주지 않는 이유: 워커 스레드가 동시에 좌석을 바꿀 수 있기 때문. */
size_t flight_store_list(flight_t *out, size_t cap);

/* 좌석 예약. 성공 시 FS_OK와 함께 out_id에 예약번호를 써준다.
 * 실패: FS_NOT_FOUND(404) / FS_SOLD_OUT(409) / FS_BAD_REQUEST(400).
 * lock_enabled 값을 보고 락을 걸거나(after) 의도적으로 생략(before)한다. */
flight_result_t flight_store_reserve(const char *id, size_t id_len,
                                     const char *name, size_t name_len,
                                     const char *contact, size_t contact_len,
                                     const char *client_id, size_t client_len,
                                     char *out_id, size_t out_cap);

/* client_id 소유 예약 목록(취소건 포함)을 out에 복사. 개수 반환. */
size_t flight_store_reservations(const char *client_id, size_t client_len,
                                 reservation_t *out, size_t cap);

/* 예약 취소: 소유자 확인 후 취소 표시 + 좌석 1석 복원. */
flight_result_t flight_store_cancel(const char *res_id, size_t res_id_len,
                                    const char *client_id, size_t client_len);

/* 시간 기반 자동 전이: now 시각 기준으로 출발/도착 상태를 한 번씩 갱신.
 * 반환 = 이번 tick에서 발생한 전이 수. 여러 스레드가 동시에 호출해도 중복 기록 안 됨. */
int flight_store_tick(long now);

/* 운영자 지연 입력: 예정 출발/도착을 delay_min분 밀고 DELAYED 상태로 변경. */
flight_result_t flight_store_delay(const char *id, size_t id_len,
                                   long delay_min, long now);

/* 상태 변경 + 타임라인 기록. at_epoch=0이면 현재 시각 사용.
 * at_epoch가 actual_depart/arrive를 갱신할 수 있음.
 * 도착이 출발보다 이전이거나 상태 역행이면 FS_BAD_REQUEST. */
flight_result_t flight_store_set_status(const char *id, size_t id_len,
                                        flight_status_t status, long at_epoch,
                                        const char *note, size_t note_len);

/* 해당 항공편의 타임라인을 out에 복사(최대 cap개). 복사한 개수 반환. */
size_t flight_store_timeline(const char *id, size_t id_len,
                             flight_event_t *out, size_t cap);

/* 현재 락 모드 / 모드 변경 (admin API용). */
int  flight_store_lock_enabled(void);
void flight_store_set_lock_enabled(int enabled);

/* "DELAYED" 같은 문자열 → 열거값. 알 수 없으면 F_INVALID.
 * 반대 방향(열거값 → 문자열)도 JSON 직렬화용으로 제공. */
flight_status_t flight_status_from_string(const char *s, size_t len);
const char *flight_status_to_string(flight_status_t status);

#endif /* FLIGHT_STORE_H */
