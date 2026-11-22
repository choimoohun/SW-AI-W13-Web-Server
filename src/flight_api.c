/*
 * flight_api.c — /api/... 라우팅 + 최소 JSON 직렬화 (STEP6).
 *
 * 범용 JSON 라이브러리 없이 필요한 필드만 읽고, 문자열은 직접 이스케이프한다.
 * 응답은 항상 같은 모양: 성공은 객체, 실패는 {"error":...,"message":...}.
 * 락은 store 안에서만 잡는다. 이 파일은 store가 복사해 준 값만 직렬화한다.
 */
#include "flight_api.h"

#include "flight_store.h"
#include "flight_clock.h"
#include "http_response.h"

#include <stdarg.h>   /* va_list */
#include <stdio.h>    /* snprintf */
#include <stdlib.h>   /* strtol */
#include <string.h>   /* memcmp, memcpy, strlen */

#define PATH_CAP   2048
#define BODY_CAP   8192
#define JSON_CAP   16384
#define MAX_FLIGHTS 16
#define MAX_EVENTS  32

typedef struct {
    char data[JSON_CAP];
    size_t len;   /* JSON_CAP 이상이면 overflow 표시 */
} json_buf;

static void jb_reset(json_buf *b)
{
    b->len = 0;
    b->data[0] = '\0';
}

static void jb_add(json_buf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    size_t room;

    if (b->len >= sizeof(b->data)) {
        return;
    }
    room = sizeof(b->data) - b->len;
    va_start(ap, fmt);
    n = vsnprintf(b->data + b->len, room, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= room) {
        b->len = sizeof(b->data);   /* 잘린 응답은 보내지 않는다 */
    } else {
        b->len += (size_t)n;
    }
}

/* 문자열을 JSON 문자열로 추가. UTF-8 바이트는 그대로 통과, 제어문자만 이스케이프. */
static void jb_str(json_buf *b, const char *s, size_t len)
{
    jb_add(b, "\"");
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  jb_add(b, "\\\""); break;
            case '\\': jb_add(b, "\\\\"); break;
            case '\n': jb_add(b, "\\n");  break;
            case '\r': jb_add(b, "\\r");  break;
            case '\t': jb_add(b, "\\t");  break;
            case '\b': jb_add(b, "\\b");  break;
            case '\f': jb_add(b, "\\f");  break;
            default:
                if (c < 0x20) {
                    jb_add(b, "\\u%04x", (unsigned)c);
                } else {
                    jb_add(b, "%c", (int)c);
                }
        }
    }
    jb_add(b, "\"");
}

static void jb_cstr(json_buf *b, const char *s)
{
    jb_str(b, s, strlen(s));
}

/* 응답을 소켓에 쓰지 않고 api_response_t 버퍼로 복사한다. */
static int write_json(api_response_t *out, int status, json_buf *b)
{
    if (b->len >= sizeof(b->data) || b->len >= sizeof(out->body)) {
        out->status = 500;
        out->len = 0;
        return 0;
    }
    out->status = status;
    memcpy(out->body, b->data, b->len);
    out->len = b->len;
    return 0;
}

static int write_error(api_response_t *out, int status, const char *code, const char *msg)
{
    json_buf b;
    jb_reset(&b);
    jb_add(&b, "{\"error\":");
    jb_cstr(&b, code);
    jb_add(&b, ",\"message\":");
    jb_cstr(&b, msg);
    jb_add(&b, "}");
    return write_json(out, status, &b);
}

/* ----- 최소 JSON 읽기 유틸 (NUL 가정 없음) ----- */

static const char *find_sub(const char *hay, size_t haylen, const char *needle, size_t nlen)
{
    if (nlen == 0 || haylen < nlen) {
        return NULL;
    }
    for (size_t i = 0; i + nlen <= haylen; ++i) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

static void skip_ws(const char *s, size_t len, size_t *i)
{
    while (*i < len && (s[*i] == ' ' || s[*i] == '\t' ||
                        s[*i] == '\r' || s[*i] == '\n')) {
        ++(*i);
    }
}

/* "key" 뒤의 : 까지 찾아 값 시작 위치를 돌려준다. */
static const char *find_value(const char *buf, size_t len, const char *key)
{
    char pattern[64];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p;
    size_t i;

    if (pn < 0 || (size_t)pn >= sizeof(pattern)) {
        return NULL;
    }
    p = find_sub(buf, len, pattern, (size_t)pn);
    if (p == NULL) {
        return NULL;
    }
    i = (size_t)(p - buf) + (size_t)pn;
    skip_ws(buf, len, &i);
    if (i >= len || buf[i] != ':') {
        return NULL;
    }
    ++i;
    skip_ws(buf, len, &i);
    return (i < len) ? (buf + i) : NULL;
}

static int json_get_string(const char *buf, size_t len, const char *key,
                           char *out, size_t cap)
{
    const char *v = find_value(buf, len, key);
    size_t i, o = 0;

    if (v == NULL || *v != '"') {
        return -1;
    }
    i = (size_t)(v - buf) + 1;
    while (i < len && buf[i] != '"') {
        char c = buf[i];
        if (c == '\\' && i + 1 < len) {
            ++i;
            c = buf[i];
            switch (c) {
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'u':  c = '?'; i += 4; break;   /* 유니코드는 '?'로 치환 */
                default: break;
            }
        }
        if (o + 1 < cap) {
            out[o++] = c;
        }
        ++i;
    }
    if (i >= len) {
        return -1;   /* 닫는 따옴표 없음 */
    }
    out[o] = '\0';
    return 0;
}

static int json_get_long(const char *buf, size_t len, const char *key, long *out)
{
    const char *v = find_value(buf, len, key);
    size_t i = 0;
    char tmp[32];
    size_t n = 0;
    char *end;

    if (v == NULL) {
        return -1;
    }
    i = (size_t)(v - buf);
    if (buf[i] == '-' && i + 1 < len) {
        tmp[n++] = buf[i++];
    }
    while (i < len && buf[i] >= '0' && buf[i] <= '9' && n + 1 < sizeof(tmp)) {
        tmp[n++] = buf[i++];
    }
    if (n == 0) {
        return -1;
    }
    tmp[n] = '\0';
    *out = strtol(tmp, &end, 10);
    return (*end == '\0') ? 0 : -1;
}

static int json_get_bool(const char *buf, size_t len, const char *key, int *out)
{
    const char *v = find_value(buf, len, key);
    if (v == NULL) {
        return -1;
    }
    if (len - (size_t)(v - buf) >= 4 && memcmp(v, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (len - (size_t)(v - buf) >= 5 && memcmp(v, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

/* ----- 경로 유틸 ----- */

static size_t path_len(const char *uri, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (uri[i] == '?') {
            return i;   /* 쿼리스트링 제거 */
        }
    }
    return len;
}

/* URI의 쿼리스트링에서 key=value를 읽는다(값은 percent-decode하지 않음). */
static int query_get(const http_request_t *req, const char *key,
                     char *out, size_t cap)
{
    const char *q = memchr(req->uri, '?', req->uri_len);
    char pattern[64];
    const char *p;
    size_t qlen, n, vlen, i = 0;

    if (q == NULL || cap == 0) return -1;
    q++;
    qlen = req->uri_len - (size_t)(q - req->uri);
    (void)snprintf(pattern, sizeof(pattern), "%s=", key);
    n = strlen(pattern);
    p = find_sub(q, qlen, pattern, n);
    if (p == NULL) return -1;

    vlen = 0;
    while ((size_t)(p - q) + n + vlen < qlen &&
           q[(size_t)(p - q) + n + vlen] != '&') {
        vlen++;
    }
    p += n;
    while (i < vlen && i + 1 < cap) out[i++] = *p++;
    out[i] = '\0';
    return 0;
}

static int path_eq(const char *path, size_t len, const char *lit)
{
    size_t n = strlen(lit);
    return (len == n && memcmp(path, lit, n) == 0);
}

static int has_prefix(const char *s, size_t len, const char *pfx, size_t *consumed)
{
    size_t n = strlen(pfx);
    if (len < n || memcmp(s, pfx, n) != 0) {
        return 0;
    }
    *consumed = n;
    return 1;
}

static int find_in_snapshot(const flight_t *arr, size_t n,
                            const char *id, size_t id_len)
{
    for (size_t i = 0; i < n; ++i) {
        if (strlen(arr[i].id) == id_len && memcmp(arr[i].id, id, id_len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ----- 응답 작성 ----- */

static long delay_minutes(const flight_t *f)
{
    long diff;
    if (f->actual_arrive_epoch != 0) {
        diff = f->actual_arrive_epoch - f->arrive_epoch;
    } else if (f->actual_depart_epoch != 0) {
        diff = f->actual_depart_epoch - f->depart_epoch;
    } else {
        return 0;
    }
    return diff / 60;
}

static void write_flight(json_buf *b, const flight_t *f)
{
    jb_add(b, "{\"id\":");            jb_cstr(b, f->id);
    jb_add(b, ",\"from\":");          jb_cstr(b, f->from);
    jb_add(b, ",\"to\":");            jb_cstr(b, f->to);
    jb_add(b, ",\"depart_epoch\":%ld", f->depart_epoch);
    jb_add(b, ",\"arrive_epoch\":%ld", f->arrive_epoch);
    jb_add(b, ",\"actual_depart_epoch\":%ld", f->actual_depart_epoch);
    jb_add(b, ",\"actual_arrive_epoch\":%ld", f->actual_arrive_epoch);
    jb_add(b, ",\"delay_min\":%ld,\"delay_applied_min\":%ld",
           delay_minutes(f), f->delay_applied_min);
    jb_add(b, ",\"seat_total\":%d", f->seat_total);
    jb_add(b, ",\"seat_left\":%d", f->seat_left);
    jb_add(b, ",\"status\":\"");
    jb_add(b, "%s", flight_status_to_string(f->status));
    jb_add(b, "\"}");
}

static int handle_list(api_response_t *out, const http_request_t *req)
{
    flight_t flights[MAX_FLIGHTS];
    char from[16] = "";
    char to[16] = "";
    size_t n = flight_store_list(flights, MAX_FLIGHTS);
    json_buf b;
    int first = 1;

    (void)query_get(req, "from", from, sizeof(from));
    (void)query_get(req, "to", to, sizeof(to));

    jb_reset(&b);
    jb_add(&b, "{\"flights\":[");
    for (size_t i = 0; i < n; ++i) {
        if (from[0] != '\0' && strcmp(flights[i].from, from) != 0) continue;
        if (to[0] != '\0' && strcmp(flights[i].to, to) != 0) continue;
        if (!first) jb_add(&b, ",");
        write_flight(&b, &flights[i]);
        first = 0;
    }
    jb_add(&b, "]}");
    return write_json(out, 200, &b);
}

static int handle_detail(api_response_t *out, const char *id, size_t id_len)
{
    flight_t flights[MAX_FLIGHTS];
    size_t n = flight_store_list(flights, MAX_FLIGHTS);
    int idx = find_in_snapshot(flights, n, id, id_len);
    json_buf b;

    if (idx < 0) {
        return write_error(out, 404, "NOT_FOUND", "flight not found");
    }
    jb_reset(&b);
    write_flight(&b, &flights[idx]);
    return write_json(out, 200, &b);
}

static int handle_reserve(api_response_t *out, const char *id, size_t id_len,
                          const http_request_t *req)
{
    char name[64];
    char contact[64] = "";
    char client[64] = "";
    char res_id[32];
    flight_result_t rc;
    flight_t flights[MAX_FLIGHTS];
    size_t n;
    int idx;
    json_buf b;

    if (req->body_len > BODY_CAP) {
        return write_error(out, 413, "PAYLOAD_TOO_LARGE", "body too large");
    }
    if (json_get_string(req->body, req->body_len, "name", name, sizeof(name)) < 0 ||
        name[0] == '\0') {
        return write_error(out, 400, "BAD_REQUEST", "name is required");
    }

    (void)json_get_string(req->body, req->body_len, "contact", contact, sizeof(contact));
    (void)json_get_string(req->body, req->body_len, "client", client, sizeof(client));
    if (client[0] == '\0') {
        (void)query_get(req, "client", client, sizeof(client));
    }
    rc = flight_store_reserve(id, id_len, name, strlen(name),
                              contact, strlen(contact),
                              client, strlen(client),
                              res_id, sizeof(res_id));
    if (rc != FS_OK) {
        if (rc == FS_NOT_FOUND) {
            return write_error(out, 404, "NOT_FOUND", "flight not found");
        }
        if (rc == FS_SOLD_OUT) {
            return write_error(out, 409, "SOLD_OUT", "no seats left");
        }
        return write_error(out, 400, "BAD_REQUEST", "reservation rejected");
    }

    n = flight_store_list(flights, MAX_FLIGHTS);
    idx = find_in_snapshot(flights, n, id, id_len);
    jb_reset(&b);
    jb_add(&b, "{\"reservation\":{\"id\":");
    jb_cstr(&b, res_id);
    jb_add(&b, ",\"flight_id\":");
    jb_str(&b, id, id_len);   /* id는 NUL 종료를 가정하지 않는다 */
    jb_add(&b, ",\"name\":");
    jb_str(&b, name, strlen(name));
    jb_add(&b, ",\"contact\":");
    jb_str(&b, contact, strlen(contact));
    jb_add(&b, "},\"seat_left\":%d}", idx >= 0 ? flights[idx].seat_left : 0);
    return write_json(out, 201, &b);
}

static void write_reservation(json_buf *b, const reservation_t *r)
{
    jb_add(b, "{\"id\":");
    jb_cstr(b, r->id);
    jb_add(b, ",\"flight_id\":");
    jb_cstr(b, r->flight_id);
    jb_add(b, ",\"name\":");
    jb_str(b, r->name, strlen(r->name));
    jb_add(b, ",\"contact\":");
    jb_str(b, r->contact, strlen(r->contact));
    jb_add(b, ",\"created_epoch\":%ld,\"cancelled\":%s}",
           r->created_epoch, r->cancelled ? "true" : "false");
}

static int handle_reservations(api_response_t *out, const http_request_t *req)
{
    reservation_t list[64];
    char client[64] = "";
    size_t n;

    if (query_get(req, "client", client, sizeof(client)) < 0 || client[0] == '\0') {
        return write_error(out, 400, "BAD_REQUEST", "client is required");
    }
    n = flight_store_reservations(client, strlen(client), list, 64);
    json_buf b;
    jb_reset(&b);
    jb_add(&b, "{\"reservations\":[");
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) jb_add(&b, ",");
        write_reservation(&b, &list[i]);
    }
    jb_add(&b, "]}");
    return write_json(out, 200, &b);
}

static int handle_cancel(api_response_t *out, const char *res_id, size_t res_id_len,
                         const http_request_t *req)
{
    char client[64] = "";
    flight_result_t rc;

    (void)json_get_string(req->body, req->body_len, "client", client, sizeof(client));
    if (client[0] == '\0') {
        (void)query_get(req, "client", client, sizeof(client));
    }
    rc = flight_store_cancel(res_id, res_id_len, client, strlen(client));
    if (rc == FS_NOT_FOUND) return write_error(out, 404, "NOT_FOUND", "reservation not found");
    if (rc != FS_OK) return write_error(out, 400, "BAD_REQUEST", "cancel rejected");
    {
        json_buf b;
        jb_reset(&b);
        jb_add(&b, "{\"ok\":true}");
        return write_json(out, 200, &b);
    }
}

static int handle_timeline(api_response_t *out, const char *id, size_t id_len)
{
    flight_t flights[MAX_FLIGHTS];
    flight_event_t events[MAX_EVENTS];
    size_t nf = flight_store_list(flights, MAX_FLIGHTS);
    size_t ne;
    json_buf b;

    if (find_in_snapshot(flights, nf, id, id_len) < 0) {
        return write_error(out, 404, "NOT_FOUND", "flight not found");
    }
    ne = flight_store_timeline(id, id_len, events, MAX_EVENTS);
    jb_reset(&b);
    jb_add(&b, "{\"events\":[");
    for (size_t i = 0; i < ne; ++i) {
        if (i > 0) {
            jb_add(&b, ",");
        }
        jb_add(&b, "{\"ts\":%ld,\"status\":\"%s\",\"note\":",
               events[i].ts, flight_status_to_string(events[i].status));
        jb_str(&b, events[i].note, strlen(events[i].note));
        jb_add(&b, "}");
    }
    jb_add(&b, "]}");
    return write_json(out, 200, &b);
}

static int handle_status(api_response_t *out, const char *id, size_t id_len,
                          const http_request_t *req)
{
    char status_str[32];
    char note[128];
    long at = 0;
    flight_status_t status;
    flight_result_t rc;
    flight_t flights[MAX_FLIGHTS];
    size_t n;
    int idx;
    json_buf b;

    if (req->body_len > BODY_CAP) {
        return write_error(out, 413, "PAYLOAD_TOO_LARGE", "body too large");
    }
    if (json_get_string(req->body, req->body_len, "status",
                        status_str, sizeof(status_str)) < 0) {
        return write_error(out, 400, "BAD_REQUEST", "status is required");
    }
    status = flight_status_from_string(status_str, strlen(status_str));
    if (status == F_INVALID) {
        return write_error(out, 400, "BAD_REQUEST", "unknown status");
    }
    note[0] = '\0';
    {
        char note_buf[128];
        note_buf[0] = '\0';
        if (json_get_string(req->body, req->body_len, "note",
                            note_buf, sizeof(note_buf)) == 0) {
            memcpy(note, note_buf, sizeof(note_buf));
        }
    }
    (void)json_get_long(req->body, req->body_len, "at", &at);

    rc = flight_store_set_status(id, id_len, status, at, note, strlen(note));
    if (rc == FS_NOT_FOUND) {
        return write_error(out, 404, "NOT_FOUND", "flight not found");
    }
    if (rc != FS_OK) {
        return write_error(out, 400, "BAD_REQUEST", "invalid status transition");
    }

    n = flight_store_list(flights, MAX_FLIGHTS);
    idx = find_in_snapshot(flights, n, id, id_len);
    jb_reset(&b);
    if (idx < 0) {
        jb_add(&b, "{\"ok\":true}");
    } else {
        jb_add(&b, "{\"ok\":true,\"flight\":");
        write_flight(&b, &flights[idx]);
        jb_add(&b, "}");
    }
    return write_json(out, 200, &b);
}

static int handle_ops_clock(api_response_t *out)
{
    json_buf b;
    jb_reset(&b);
    jb_add(&b, "{\"now\":%ld,\"mode\":\"%s\",\"speed\":%ld}",
           flight_clock_now(), flight_clock_is_sim() ? "sim" : "real",
           flight_clock_speed());
    return write_json(out, 200, &b);
}

static int handle_delay(api_response_t *out, const char *id, size_t id_len,
                        const http_request_t *req)
{
    long delay_min = 0;
    flight_result_t rc;

    if (req->body_len > BODY_CAP) {
        return write_error(out, 413, "PAYLOAD_TOO_LARGE", "body too large");
    }
    if (json_get_long(req->body, req->body_len, "delay_min", &delay_min) < 0 ||
        delay_min <= 0) {
        return write_error(out, 400, "BAD_REQUEST", "delay_min must be positive");
    }
    rc = flight_store_delay(id, id_len, delay_min, flight_clock_now());
    if (rc == FS_NOT_FOUND) return write_error(out, 404, "NOT_FOUND", "flight not found");
    if (rc != FS_OK) return write_error(out, 400, "BAD_REQUEST", "delay rejected");

    {
        flight_t flights[MAX_FLIGHTS];
        size_t n = flight_store_list(flights, MAX_FLIGHTS);
        int idx = find_in_snapshot(flights, n, id, id_len);
        json_buf b;
        jb_reset(&b);
        if (idx < 0) {
            jb_add(&b, "{\"ok\":true}");
        } else {
            jb_add(&b, "{\"ok\":true,\"flight\":");
            write_flight(&b, &flights[idx]);
            jb_add(&b, "}");
        }
        return write_json(out, 200, &b);
    }
}

static int handle_mode_get(api_response_t *out)
{
    json_buf b;
    jb_reset(&b);
    jb_add(&b, "{\"lock\":%s}", flight_store_lock_enabled() ? "true" : "false");
    return write_json(out, 200, &b);
}

static int handle_mode_post(api_response_t *out, const http_request_t *req)
{
    int lock;

    if (req->body_len > BODY_CAP) {
        return write_error(out, 413, "PAYLOAD_TOO_LARGE", "body too large");
    }
    if (json_get_bool(req->body, req->body_len, "lock", &lock) < 0) {
        return write_error(out, 400, "BAD_REQUEST", "lock must be true or false");
    }
    flight_store_set_lock_enabled(lock);
    return handle_mode_get(out);
}

int flight_api_builtin(const http_request_t *req, api_response_t *out)
{
    json_buf b;

    if (req == NULL) return -1;
    if (path_eq(req->uri, req->uri_len, "/health")) {
        jb_reset(&b);
        jb_add(&b, "{\"status\":\"ok\",\"service\":\"jungle-c\"}");
        return write_json(out, 200, &b);
    }
    if (path_eq(req->uri, req->uri_len, "/echo")) {
        int is_post = (req->method_len == 4 && memcmp(req->method, "POST", 4) == 0);
        if (!is_post) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use POST");
        if (req->body_len > BODY_CAP) {
            return write_error(out, 413, "PAYLOAD_TOO_LARGE", "body too large");
        }
        jb_reset(&b);
        jb_add(&b, "{\"echo\":");
        jb_str(&b, req->body == NULL ? "" : req->body, req->body_len);
        jb_add(&b, "}");
        return write_json(out, 200, &b);
    }
    return -1;
}

int flight_api_is_api_uri(const char *uri, size_t uri_len)
{
    const char prefix[] = "/api/";
    const size_t prefix_len = sizeof(prefix) - 1;

    if (uri == NULL || uri_len < prefix_len) {
        return 0;
    }
    for (size_t i = 0; i < prefix_len; ++i) {
        if (uri[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

int flight_api_build(const http_request_t *req, api_response_t *out)
{
    char path[PATH_CAP];
    size_t plen, used;
    int is_get, is_post;

    if (req == NULL || !flight_api_is_api_uri(req->uri, req->uri_len)) {
        return -1;
    }

    plen = path_len(req->uri, req->uri_len);
    if (plen >= sizeof(path)) {
        return write_error(out, 414, "URI_TOO_LONG", "uri too long");
    }
    memcpy(path, req->uri, plen);
    path[plen] = '\0';

    is_get = (req->method_len == 3 && memcmp(req->method, "GET", 3) == 0);
    is_post = (req->method_len == 4 && memcmp(req->method, "POST", 4) == 0);

    if (path_eq(path, plen, "/api/flights")) {
        if (!is_get) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET");
        return handle_list(out, req);
    }

    if (path_eq(path, plen, "/api/reservations")) {
        if (!is_get) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET");
        return handle_reservations(out, req);
    }

    if (has_prefix(path, plen, "/api/reservations/", &used)) {
        const char *rest = path + used;
        size_t rest_len = plen - used;
        const char *slash = memchr(rest, '/', rest_len);
        if (slash != NULL &&
            path_eq(slash, rest_len - (size_t)(slash - rest), "/cancel")) {
            if (!is_post) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use POST");
            return handle_cancel(out, rest, (size_t)(slash - rest), req);
        }
        return write_error(out, 404, "NOT_FOUND", "unknown api path");
    }

    if (path_eq(path, plen, "/api/ops/clock")) {
        if (!is_get) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET");
        return handle_ops_clock(out);
    }

    if (has_prefix(path, plen, "/api/ops/flights/", &used)) {
        const char *rest = path + used;
        size_t rest_len = plen - used;
        const char *slash = memchr(rest, '/', rest_len);
        if (slash != NULL &&
            path_eq(slash, rest_len - (size_t)(slash - rest), "/delay")) {
            if (!is_post) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use POST");
            return handle_delay(out, rest, (size_t)(slash - rest), req);
        }
        return write_error(out, 404, "NOT_FOUND", "unknown api path");
    }

    if (path_eq(path, plen, "/api/admin/mode")) {
        if (is_get)  return handle_mode_get(out);
        if (is_post) return handle_mode_post(out, req);
        return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET or POST");
    }

    if (has_prefix(path, plen, "/api/flights/", &used)) {
        const char *rest = path + used;
        size_t rest_len = plen - used;

        if (rest_len == 0) {
            return write_error(out, 404, "NOT_FOUND", "flight not found");
        }
        /* /{id}/reserve, /{id}/timeline, /{id} */
        const char *slash = memchr(rest, '/', rest_len);
        if (slash == NULL) {
            if (!is_get) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET");
            return handle_detail(out, rest, rest_len);
        }
        size_t id_len = (size_t)(slash - rest);
        const char *suffix = slash;
        size_t suffix_len = rest_len - id_len;
        if (path_eq(suffix, suffix_len, "/reserve")) {
            if (!is_post) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use POST");
            return handle_reserve(out, rest, id_len, req);
        }
        if (path_eq(suffix, suffix_len, "/timeline")) {
            if (!is_get) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use GET");
            return handle_timeline(out, rest, id_len);
        }
        return write_error(out, 404, "NOT_FOUND", "unknown api path");
    }

    if (has_prefix(path, plen, "/api/admin/flights/", &used)) {
        const char *rest = path + used;
        size_t rest_len = plen - used;
        const char *slash = memchr(rest, '/', rest_len);

        if (slash != NULL && path_eq(slash, rest_len - (size_t)(slash - rest), "/status")) {
            if (!is_post) return write_error(out, 405, "METHOD_NOT_ALLOWED", "use POST");
            return handle_status(out, rest, (size_t)(slash - rest), req);
        }
        return write_error(out, 404, "NOT_FOUND", "unknown api path");
    }

    return write_error(out, 404, "NOT_FOUND", "unknown api path");
}
