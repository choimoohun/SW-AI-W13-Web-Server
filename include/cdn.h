/*
 * cdn.h — 로컬 미니 CDN(정적 자산 TTL 캐시).
 * PDF의 CDN 개념(TTL, cache hit/miss, 무효화)을 외부 서비스 없이 재현한다.
 */
#ifndef CDN_H
#define CDN_H

#include <stddef.h>

/* hit이면 1, miss면 0. 데이터를 out에 복사하고 길이를 *out_len에 둔다. */
int cdn_lookup(const char *key, char *out, size_t cap, size_t *out_len);

/* ttl_sec 초 동안 캐시. 같은 key는 덮어쓴다. */
void cdn_store(const char *key, const char *data, size_t len, int ttl_sec);

/* key를 가진 항목 제거(무효화/purge). 없으면 0. */
int cdn_purge(const char *key);

#endif /* CDN_H */
