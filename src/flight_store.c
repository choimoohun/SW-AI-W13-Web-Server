/*
 * flight_store.c — 항공편 인메모리 저장소 구현 (STEP6).
 *
 * [동시성 요약 — 항공편별 락]
 * - 각 flight_t가 자기 뮤텍스를 가진다. 좌석/상태/타임라인은 그 락으로 보호한다.
 * - g_store.mu는 메타데이터(예약 목록, lock_enabled, 데이터 경로)만 보호한다.
 * - 락 순서: flight.mu → g_store.mu. 취소는 g_store.mu를 먼저 풀고 flight.mu를 든다.
 * - 파일 저장은 락을 든 상태에서 하지 않는다. 스냅샷을 만든 뒤 락을 풀고 기록한다.
 * - before 모드: 예약 경로만 flight.mu를 생략하고 check/decrement 사이에
 *   usleep(20000)을 둔다. → 워커가 2개 이상이면 초과 예약이 재현된다(의도된 UB).
 */
#include "flight_store.h"

#include <stdio.h>    /* snprintf, fopen */
#include <string.h>   /* memset, memcpy, strlen, memcmp */
#include <stdlib.h>   /* getenv */
#include <sys/stat.h> /* mkdir */
#include <time.h>     /* time */
#include <unistd.h>   /* close 등 */

#define MAX_FLIGHTS      16
#define MAX_RESERVATIONS 256
#define MAX_EVENTS       32
#define ID_CAP           16
#define NAME_CAP         64
#define NOTE_CAP         128
#define CLIENT_CAP       64

typedef struct {
    flight_t flights[MAX_FLIGHTS];
    reservation_t reservations[MAX_RESERVATIONS];
    flight_event_t events[MAX_FLIGHTS][MAX_EVENTS];
    size_t event_count[MAX_FLIGHTS];
    size_t flight_count;
    size_t reservation_count;
} store_snapshot_t;

static flight_store_t g_store;
static char g_data_path[256] = "data/flights.txt";
static store_snapshot_t g_snapshot;                       /* 저장용 공용 스냅샷 */
static pthread_mutex_t g_save_mu = PTHREAD_MUTEX_INITIALIZER; /* 스냅샷/쓰기 직렬화 */

/* id는 NUL 종료를 가정하지 않는다: 길이를 넘겨 정확히 비교. */
static int find_index(const char *id, size_t id_len)
{
    if (id == NULL || id_len == 0 || id_len >= ID_CAP) {
        return -1;
    }
    for (size_t i = 0; i < g_store.flight_count; ++i) {
        if (strlen(g_store.flights[i].id) == id_len &&
            memcmp(g_store.flights[i].id, id, id_len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void copy_str_n(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap) {
        len = cap - 1;
    }
    if (len > 0 && src != NULL) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

/* pthread_mutex_t는 복사하지 않는다. 락 객체는 원본만 소유한다. */
static void copy_flight(flight_t *d, const flight_t *s)
{
    memcpy(d->id, s->id, sizeof(d->id));
    memcpy(d->from, s->from, sizeof(d->from));
    memcpy(d->to, s->to, sizeof(d->to));
    d->depart_epoch = s->depart_epoch;
    d->arrive_epoch = s->arrive_epoch;
    d->actual_depart_epoch = s->actual_depart_epoch;
    d->actual_arrive_epoch = s->actual_arrive_epoch;
    d->delay_applied_min = s->delay_applied_min;
    d->seat_total = s->seat_total;
    d->seat_left = s->seat_left;
    d->status = s->status;
}

/* 상태 전이 순위. 역행(ARRIVED → SCHEDULED 등)을 거부하는 데 쓴다. */
static int status_rank(flight_status_t s)
{
    switch (s) {
        case F_SCHEDULED: return 0;
        case F_BOARDING:  return 1;
        case F_DELAYED:   return 2;
        case F_DEPARTED:  return 3;
        case F_ARRIVED:   return 4;
        default:          return -1;
    }
}

static void seed_flight(int idx, const char *id, const char *from, const char *to,
                        long depart, long arrive, int seats)
{
    flight_t *f = &g_store.flights[idx];
    flight_event_t *ev = &g_store.events[idx][0];

    memset(f, 0, sizeof(*f));
    copy_str(f->id, sizeof(f->id), id);
    copy_str(f->from, sizeof(f->from), from);
    copy_str(f->to, sizeof(f->to), to);
    f->depart_epoch = depart;
    f->arrive_epoch = arrive;
    f->seat_total = seats;
    f->seat_left = seats;
    f->status = F_SCHEDULED;

    memset(ev, 0, sizeof(*ev));
    ev->ts = depart - 86400;   /* 출발 하루 전 스케줄 등록 */
    ev->status = F_SCHEDULED;
    copy_str(ev->note, sizeof(ev->note), "스케줄 등록");
    g_store.event_count[idx] = 1;
}

/* ----- 파일 영속 저장 (외부 라이브러리 없이 '|' 구분 텍스트) ----- */

void flight_store_set_data_path(const char *path)
{
    if (path == NULL || path[0] == '\0') return;
    (void)snprintf(g_data_path, sizeof(g_data_path), "%s", path);
}

static void write_field(FILE *fp, const char *s)
{
    for (; *s != '\0'; ++s) {
        if (*s == '|' || *s == '\\' || *s == '\n' || *s == '\r') {
            fputc('\\', fp);
        }
        fputc(*s, fp);
    }
}

/* '|'로 토큰 분리와 동시에 이스케이프를 푼다(버퍼를 제자리에서 수정). */
static int split_fields(char *line, char **out, int max)
{
    int n = 0;
    char *src = line;
    char *dst = line;

    out[n++] = dst;
    while (*src != '\0') {
        if (*src == '\\' && src[1] != '\0') {
            src++;
            *dst++ = *src++;
        } else if (*src == '|') {
            *dst++ = '\0';
            src++;
            if (n >= max) return n;
            out[n++] = dst;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return n;
}

/* 스냅샷을 만든 뒤 락을 모두 풀고 파일에 쓴다. 디스크 I/O로 다른 워커를 막지 않는다. */
static void snapshot_and_save(void)
{
    store_snapshot_t *s = &g_snapshot;

    if (strcmp(g_data_path, "/dev/null") == 0) return;

    pthread_mutex_lock(&g_save_mu);

    /* 락 순서: 각 항공편 → 메타. reserve와 같은 순서라 데드락이 없다. */
    for (size_t i = 0; i < g_store.flight_count; ++i) {
        pthread_mutex_lock(&g_store.flights[i].mu);
    }
    pthread_mutex_lock(&g_store.mu);

    s->flight_count = g_store.flight_count;
    s->reservation_count = g_store.reservation_count;
    memcpy(s->reservations, g_store.reservations,
           s->reservation_count * sizeof(s->reservations[0]));
    for (size_t i = 0; i < s->flight_count; ++i) {
        copy_flight(&s->flights[i], &g_store.flights[i]);
        s->event_count[i] = g_store.event_count[i];
        memcpy(s->events[i], g_store.events[i],
               s->event_count[i] * sizeof(s->events[i][0]));
    }

    pthread_mutex_unlock(&g_store.mu);
    for (size_t i = g_store.flight_count; i > 0; --i) {
        pthread_mutex_unlock(&g_store.flights[i - 1].mu);
    }

    {
        char tmp[sizeof(g_data_path) + 8];
        FILE *fp;

        (void)snprintf(tmp, sizeof(tmp), "%s.tmp", g_data_path);
        fp = fopen(tmp, "w");
        if (fp != NULL) {
            for (size_t i = 0; i < s->flight_count; ++i) {
                const flight_t *f = &s->flights[i];
                fprintf(fp, "F|%s|%s|%s|%ld|%ld|%ld|%ld|%d|%d|%d|%ld\n",
                        f->id, f->from, f->to, f->depart_epoch, f->arrive_epoch,
                        f->actual_depart_epoch, f->actual_arrive_epoch,
                        f->seat_total, f->seat_left, (int)f->status,
                        f->delay_applied_min);
            }
            for (size_t i = 0; i < s->reservation_count; ++i) {
                const reservation_t *r = &s->reservations[i];
                fprintf(fp, "R|%s|%s|", r->id, r->flight_id);
                write_field(fp, r->name);
                fputc('|', fp);
                write_field(fp, r->contact);
                fputc('|', fp);
                write_field(fp, r->client_id);
                fprintf(fp, "|%ld|%d\n", r->created_epoch, r->cancelled);
            }
            for (size_t i = 0; i < s->flight_count; ++i) {
                for (size_t j = 0; j < s->event_count[i]; ++j) {
                    const flight_event_t *ev = &s->events[i][j];
                    fprintf(fp, "E|%zu|%ld|%d|", i, ev->ts, (int)ev->status);
                    write_field(fp, ev->note);
                    fputc('\n', fp);
                }
            }
            fclose(fp);
            (void)rename(tmp, g_data_path);
        }
    }
    pthread_mutex_unlock(&g_save_mu);
}

static void store_load(void)
{
    FILE *fp = fopen(g_data_path, "r");
    char line[1024];
    size_t nflights = 0;

    if (fp == NULL) return;
    memset(&g_store, 0, sizeof(g_store));
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *f[16];
        int n;
        size_t k;

        line[strcspn(line, "\r\n")] = '\0';
        n = split_fields(line, f, 16);
        if (n < 2) continue;

        if (strcmp(f[0], "F") == 0 && n >= 11 && nflights < MAX_FLIGHTS) {
            flight_t *fl = &g_store.flights[nflights++];
            memset(fl, 0, sizeof(*fl));
            copy_str(fl->id, sizeof(fl->id), f[1]);
            copy_str(fl->from, sizeof(fl->from), f[2]);
            copy_str(fl->to, sizeof(fl->to), f[3]);
            fl->depart_epoch = strtol(f[4], NULL, 10);
            fl->arrive_epoch = strtol(f[5], NULL, 10);
            fl->actual_depart_epoch = strtol(f[6], NULL, 10);
            fl->actual_arrive_epoch = strtol(f[7], NULL, 10);
            fl->seat_total = (int)strtol(f[8], NULL, 10);
            fl->seat_left = (int)strtol(f[9], NULL, 10);
            fl->status = (flight_status_t)strtol(f[10], NULL, 10);
            fl->delay_applied_min = (n >= 12) ? strtol(f[11], NULL, 10) : 0;
        } else if (strcmp(f[0], "R") == 0 && n >= 8 &&
                   g_store.reservation_count < MAX_RESERVATIONS) {
            reservation_t *r = &g_store.reservations[g_store.reservation_count++];
            memset(r, 0, sizeof(*r));
            copy_str(r->id, sizeof(r->id), f[1]);
            copy_str(r->flight_id, sizeof(r->flight_id), f[2]);
            copy_str(r->name, sizeof(r->name), f[3]);
            copy_str(r->contact, sizeof(r->contact), f[4]);
            copy_str(r->client_id, sizeof(r->client_id), f[5]);
            r->created_epoch = strtol(f[6], NULL, 10);
            r->cancelled = (int)strtol(f[7], NULL, 10);
        } else if (strcmp(f[0], "E") == 0 && n >= 5) {
            k = (size_t)strtol(f[1], NULL, 10);
            if (k < MAX_FLIGHTS && g_store.event_count[k] < MAX_EVENTS) {
                flight_event_t *ev = &g_store.events[k][g_store.event_count[k]++];
                ev->ts = strtol(f[2], NULL, 10);
                ev->status = (flight_status_t)strtol(f[3], NULL, 10);
                copy_str(ev->note, sizeof(ev->note), f[4]);
            }
        }
    }
    fclose(fp);
    g_store.flight_count = nflights;
}

void flight_store_init(void)
{
    const char *env = getenv("FLIGHT_DATA_FILE");
    int seeded = 0;

    if (env != NULL && env[0] != '\0') {
        (void)snprintf(g_data_path, sizeof(g_data_path), "%s", env);
    }
    /* 시작 시점에는 워커가 없으므로 락 없이 로드한 뒤 뮤텍스를 초기화한다. */
    memset(&g_store, 0, sizeof(g_store));
    if (strcmp(g_data_path, "/dev/null") != 0) {
        (void)mkdir("data", 0755);
        store_load();
    }
    (void)pthread_mutex_init(&g_store.mu, NULL);
    g_store.lock_enabled = 1;   /* after가 기본. before는 실행 중 admin API로만 전환 */

    if (g_store.flight_count == 0) {
        seed_flight(0, "KE701", "ICN", "NRT", 1795424400L, 1795433400L, 3);
        seed_flight(1, "KE999", "ICN", "JFK", 1795428900L, 1795438800L, 1);
        seed_flight(2, "KE202", "ICN", "NRT", 1795507200L, 1795514400L, 5);
        g_store.flight_count = 3;
        seeded = 1;
    }

    /* store_load/seed가 구조체를 memset했으므로 항공편 락은 그 뒤에 초기화. */
    for (size_t i = 0; i < g_store.flight_count; ++i) {
        (void)pthread_mutex_init(&g_store.flights[i].mu, NULL);
    }
    if (seeded) snapshot_and_save();
}

size_t flight_store_list(flight_t *out, size_t cap)
{
    size_t n = (g_store.flight_count < cap) ? g_store.flight_count : cap;

    for (size_t i = 0; i < n; ++i) {
        pthread_mutex_lock(&g_store.flights[i].mu);
        if (out != NULL) copy_flight(&out[i], &g_store.flights[i]);
        pthread_mutex_unlock(&g_store.flights[i].mu);
    }
    return n;
}

flight_result_t flight_store_reserve(const char *id, size_t id_len,
                                     const char *name, size_t name_len,
                                     const char *contact, size_t contact_len,
                                     const char *client_id, size_t client_len,
                                     char *out_id, size_t out_cap)
{
    char safe_name[NAME_CAP];
    char safe_contact[64];
    char safe_client[CLIENT_CAP];
    flight_result_t rc = FS_BAD_REQUEST;
    int use_lock;
    int flight_locked = 0;
    int idx;
    flight_t *f;

    if (name == NULL || name_len == 0 || name_len >= sizeof(safe_name)) {
        return FS_BAD_REQUEST;
    }
    copy_str_n(safe_name, sizeof(safe_name), name, name_len);
    copy_str_n(safe_contact, sizeof(safe_contact),
               contact == NULL ? "" : contact, contact == NULL ? 0 : contact_len);
    copy_str_n(safe_client, sizeof(safe_client),
               client_id == NULL ? "anonymous" : client_id,
               client_id == NULL ? 0 : client_len);
    if (safe_client[0] == '\0') copy_str(safe_client, sizeof(safe_client), "anonymous");

    /* after=항공편 락, before=의도적 무락. 기본은 after. */
    pthread_mutex_lock(&g_store.mu);
    use_lock = g_store.lock_enabled;
    pthread_mutex_unlock(&g_store.mu);

    idx = find_index(id, id_len);
    if (idx < 0) return FS_NOT_FOUND;
    f = &g_store.flights[idx];

    if (use_lock) {
        pthread_mutex_lock(&f->mu);
        flight_locked = 1;
    }

    if (f->seat_left <= 0) {
        rc = FS_SOLD_OUT;
        goto done;
    }

    /* ⚠️ before 모드 전용: 레이스 창을 인위적으로 넓힌다. 실제 데이터 레이스(UB). */
    if (!use_lock) {
        {
            struct timespec nap;
            nap.tv_sec = 0; nap.tv_nsec = 20000L * 1000L;
            (void)nanosleep(&nap, NULL);
        }
    }

    /* 락 순서: flight.mu 보유 중에 meta 락. */
    pthread_mutex_lock(&g_store.mu);
    if (g_store.reservation_count >= MAX_RESERVATIONS) {
        pthread_mutex_unlock(&g_store.mu);
        rc = FS_BAD_REQUEST;
        goto done;
    }
    {
        reservation_t *r = &g_store.reservations[g_store.reservation_count];
        memset(r, 0, sizeof(*r));
        (void)snprintf(r->id, sizeof(r->id), "RS-%04zu", g_store.reservation_count + 1);
        copy_str(r->flight_id, sizeof(r->flight_id), f->id);
        copy_str(r->name, sizeof(r->name), safe_name);
        copy_str(r->contact, sizeof(r->contact), safe_contact);
        copy_str(r->client_id, sizeof(r->client_id), safe_client);
        r->created_epoch = (long)time(NULL);
        g_store.reservation_count++;

        if (out_id != NULL && out_cap > 0) {
            (void)snprintf(out_id, out_cap, "%s", r->id);
        }
        f->seat_left--;

        if (g_store.event_count[idx] < MAX_EVENTS) {
            flight_event_t *ev = &g_store.events[idx][g_store.event_count[idx]++];
            ev->ts = r->created_epoch;
            ev->status = f->status;
            (void)snprintf(ev->note, sizeof(ev->note), "예약 완료: %s (잔여 %d석)",
                           r->id, f->seat_left);
        }
    }
    pthread_mutex_unlock(&g_store.mu);
    rc = FS_OK;

done:
    if (flight_locked) pthread_mutex_unlock(&f->mu);
    if (rc == FS_OK) snapshot_and_save();   /* 락 밖에서 파일 기록 */
    return rc;
}

/* 항공편 락을 보유한 상태에서 이벤트 추가. */
static void append_event_flight_locked(size_t idx, long ts,
                                       flight_status_t status, const char *note)
{
    if (g_store.event_count[idx] < MAX_EVENTS) {
        flight_event_t *ev = &g_store.events[idx][g_store.event_count[idx]++];
        ev->ts = ts;
        ev->status = status;
        copy_str(ev->note, sizeof(ev->note), note);
    }
}

flight_result_t flight_store_set_status(const char *id, size_t id_len,
                                        flight_status_t status, long at_epoch,
                                        const char *note, size_t note_len)
{
    flight_result_t rc = FS_BAD_REQUEST;
    int idx;
    flight_t *f;
    long now, new_dep, new_arr;
    char safe_note[NOTE_CAP];

    if (status_rank(status) < 0) return FS_BAD_REQUEST;
    copy_str_n(safe_note, sizeof(safe_note), note == NULL ? "" : note,
               note == NULL ? 0 : note_len);

    idx = find_index(id, id_len);
    if (idx < 0) return FS_NOT_FOUND;
    f = &g_store.flights[idx];

    pthread_mutex_lock(&f->mu);
    if (status_rank(status) < status_rank(f->status)) {
        goto done;   /* 상태 역행 금지 */
    }

    now = (at_epoch != 0) ? at_epoch : (long)time(NULL);
    new_dep = f->actual_depart_epoch;
    new_arr = f->actual_arrive_epoch;
    if (status == F_DEPARTED) new_dep = now;
    if (status == F_ARRIVED)  new_arr = now;
    if (new_dep != 0 && new_arr != 0 && new_arr < new_dep) goto done;

    f->status = status;
    f->actual_depart_epoch = new_dep;
    f->actual_arrive_epoch = new_arr;
    {
        const char *text = safe_note[0] != '\0'
                               ? safe_note : flight_status_to_string(status);
        append_event_flight_locked((size_t)idx, now, status, text);
    }
    rc = FS_OK;

done:
    pthread_mutex_unlock(&f->mu);
    if (rc == FS_OK) snapshot_and_save();
    return rc;
}

size_t flight_store_timeline(const char *id, size_t id_len,
                             flight_event_t *out, size_t cap)
{
    size_t n = 0;
    int idx = find_index(id, id_len);

    if (idx < 0) return 0;
    pthread_mutex_lock(&g_store.flights[idx].mu);
    n = (g_store.event_count[idx] < cap) ? g_store.event_count[idx] : cap;
    if (out != NULL && n > 0) {
        memcpy(out, g_store.events[idx], n * sizeof(*out));
    }
    pthread_mutex_unlock(&g_store.flights[idx].mu);
    return n;
}

size_t flight_store_reservations(const char *client_id, size_t client_len,
                                 reservation_t *out, size_t cap)
{
    size_t n = 0;
    char wanted[CLIENT_CAP];

    copy_str_n(wanted, sizeof(wanted),
               client_id == NULL ? "anonymous" : client_id,
               client_id == NULL ? 0 : client_len);

    pthread_mutex_lock(&g_store.mu);
    for (size_t i = 0; i < g_store.reservation_count && n < cap; ++i) {
        if (strcmp(g_store.reservations[i].client_id, wanted) == 0) {
            if (out != NULL) out[n] = g_store.reservations[i];
            n++;
        }
    }
    pthread_mutex_unlock(&g_store.mu);
    return n;
}

flight_result_t flight_store_cancel(const char *res_id, size_t res_id_len,
                                    const char *client_id, size_t client_len)
{
    flight_result_t rc = FS_NOT_FOUND;
    char wanted[CLIENT_CAP];
    char flight_id[ID_CAP];
    int found = 0;

    if (res_id == NULL || res_id_len == 0 || res_id_len >= 32) return FS_BAD_REQUEST;
    copy_str_n(wanted, sizeof(wanted),
               client_id == NULL ? "anonymous" : client_id,
               client_id == NULL ? 0 : client_len);

    /* 메타 락에서 소유자 확인 + 취소 표시. 여기서 flight 락은 잡지 않는다. */
    pthread_mutex_lock(&g_store.mu);
    for (size_t i = 0; i < g_store.reservation_count; ++i) {
        reservation_t *r = &g_store.reservations[i];
        if (strlen(r->id) != res_id_len || memcmp(r->id, res_id, res_id_len) != 0) {
            continue;
        }
        if (strcmp(r->client_id, wanted) != 0) {
            rc = FS_BAD_REQUEST;   /* 다른 사람의 예약 */
            break;
        }
        if (r->cancelled) {
            rc = FS_OK;            /* 이미 취소됨 — 멱등 */
            break;
        }
        r->cancelled = 1;
        copy_str(flight_id, sizeof(flight_id), r->flight_id);
        found = 1;
        rc = FS_OK;
        break;
    }
    pthread_mutex_unlock(&g_store.mu);

    if (found) {
        int idx = find_index(flight_id, strlen(flight_id));
        if (idx >= 0) {
            flight_t *f = &g_store.flights[idx];
            pthread_mutex_lock(&f->mu);
            if (f->seat_left < f->seat_total) f->seat_left++;
            pthread_mutex_unlock(&f->mu);
        }
        snapshot_and_save();   /* 락 밖에서 파일 기록 */
    }
    return rc;
}

int flight_store_tick(long now)
{
    int changed = 0;

    for (size_t i = 0; i < g_store.flight_count; ++i) {
        flight_t *f = &g_store.flights[i];

        pthread_mutex_lock(&f->mu);
        if (f->status != F_ARRIVED) {
            if ((f->status == F_SCHEDULED || f->status == F_BOARDING ||
                 f->status == F_DELAYED) && now >= f->depart_epoch) {
                f->status = F_DEPARTED;
                f->actual_depart_epoch = now;
                append_event_flight_locked(i, now, F_DEPARTED, "자동 출발 기록");
                changed++;
            }
            if (f->status == F_DEPARTED && now >= f->arrive_epoch) {
                f->status = F_ARRIVED;
                f->actual_arrive_epoch = now;
                append_event_flight_locked(i, now, F_ARRIVED, "자동 도착 기록");
                changed++;
            }
        }
        pthread_mutex_unlock(&f->mu);
    }
    if (changed != 0) snapshot_and_save();
    return changed;
}

flight_result_t flight_store_delay(const char *id, size_t id_len,
                                   long delay_min, long now)
{
    flight_t *f;
    int idx;
    long delta;
    char note[128];

    if (delay_min <= 0 || delay_min > 1440) return FS_BAD_REQUEST;
    idx = find_index(id, id_len);
    if (idx < 0) return FS_NOT_FOUND;
    f = &g_store.flights[idx];

    pthread_mutex_lock(&f->mu);
    if (status_rank(f->status) > status_rank(F_DELAYED)) {
        pthread_mutex_unlock(&f->mu);
        return FS_BAD_REQUEST;   /* 이미 출발/도착한 편은 지연 불가 */
    }

    delta = delay_min * 60L;
    f->delay_applied_min += delay_min;
    f->depart_epoch += delta;
    f->arrive_epoch += delta;
    f->status = F_DELAYED;
    (void)snprintf(note, sizeof(note), "지연 %ld분: 출발/도착 예정 시각 조정", delay_min);
    append_event_flight_locked((size_t)idx, now, F_DELAYED, note);
    pthread_mutex_unlock(&f->mu);

    snapshot_and_save();
    return FS_OK;
}

int flight_store_lock_enabled(void)
{
    int enabled;
    pthread_mutex_lock(&g_store.mu);
    enabled = g_store.lock_enabled;
    pthread_mutex_unlock(&g_store.mu);
    return enabled;
}

void flight_store_set_lock_enabled(int enabled)
{
    pthread_mutex_lock(&g_store.mu);
    g_store.lock_enabled = (enabled != 0);
    pthread_mutex_unlock(&g_store.mu);
}

flight_status_t flight_status_from_string(const char *s, size_t len)
{
    if (s == NULL || len == 0) {
        return F_INVALID;
    }
    if (len == 9 && memcmp(s, "SCHEDULED", 9) == 0) return F_SCHEDULED;
    if (len == 8 && memcmp(s, "BOARDING", 8) == 0)  return F_BOARDING;
    if (len == 8 && memcmp(s, "DEPARTED", 8) == 0)  return F_DEPARTED;
    if (len == 7 && memcmp(s, "DELAYED", 7) == 0)   return F_DELAYED;
    if (len == 8 && memcmp(s, "ARRIVED", 8) == 0)   return F_ARRIVED;
    return F_INVALID;
}

const char *flight_status_to_string(flight_status_t status)
{
    switch (status) {
        case F_SCHEDULED: return "SCHEDULED";
        case F_BOARDING:  return "BOARDING";
        case F_DELAYED:   return "DELAYED";
        case F_DEPARTED:  return "DEPARTED";
        case F_ARRIVED:   return "ARRIVED";
        default:          return "UNKNOWN";
    }
}
