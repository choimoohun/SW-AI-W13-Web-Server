/*
 * cdn.c — 고정 크기 인메모리 정적 자산 캐시.
 * 외부 라이브러리 없이 TTL과 purge를 구현한다(NFR-01: 동적 할당 없음).
 */
#include "cdn.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

#define CDN_SLOTS 32
#define CDN_KEY_LEN 256
#define CDN_DATA_CAP 16384

typedef struct {
    char key[CDN_KEY_LEN];
    char data[CDN_DATA_CAP];
    size_t len;
    time_t expires;
    int used;
} cdn_entry_t;

static cdn_entry_t g_entries[CDN_SLOTS];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* 가장 오래된 슬롯을 찾아 재사용한다(단순 LRU 근사: expires가 가장 빠른 것). */
static int pick_slot(void)
{
    int victim = 0;
    time_t oldest = g_entries[0].expires;

    for (int i = 0; i < CDN_SLOTS; ++i) {
        if (!g_entries[i].used) return i;
        if (g_entries[i].expires <= oldest) {
            oldest = g_entries[i].expires;
            victim = i;
        }
    }
    return victim;
}

int cdn_lookup(const char *key, char *out, size_t cap, size_t *out_len)
{
    int hit = 0;

    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < CDN_SLOTS; ++i) {
        cdn_entry_t *e = &g_entries[i];
        if (!e->used || strcmp(e->key, key) != 0) continue;
        if (e->expires < time(NULL)) {
            memset(e, 0, sizeof(*e));   /* TTL 만료 → 제거 */
            break;
        }
        if (cap >= e->len) {
            memcpy(out, e->data, e->len);
            *out_len = e->len;
            hit = 1;
        }
        break;
    }
    pthread_mutex_unlock(&g_mu);
    return hit;
}

void cdn_store(const char *key, const char *data, size_t len, int ttl_sec)
{
    if (len > CDN_DATA_CAP || len == 0) return;

    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < CDN_SLOTS; ++i) {
        cdn_entry_t *e = &g_entries[i];
        if (e->used && strcmp(e->key, key) == 0) {
            memcpy(e->data, data, len);
            e->len = len;
            e->expires = time(NULL) + ttl_sec;
            pthread_mutex_unlock(&g_mu);
            return;
        }
    }
    {
        cdn_entry_t *e = &g_entries[pick_slot()];
        memset(e, 0, sizeof(*e));
        strcpy(e->key, key);
        memcpy(e->data, data, len);
        e->len = len;
        e->expires = time(NULL) + ttl_sec;
        e->used = 1;
    }
    pthread_mutex_unlock(&g_mu);
}

int cdn_purge(const char *key)
{
    int found = 0;

    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < CDN_SLOTS; ++i) {
        if (g_entries[i].used && strcmp(g_entries[i].key, key) == 0) {
            memset(&g_entries[i], 0, sizeof(g_entries[i]));
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return found;
}
