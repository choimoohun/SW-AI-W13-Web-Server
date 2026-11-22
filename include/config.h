/*
 * config.h — 실행 설정. 기본값 → 설정 파일 → CLI 순으로 덮어쓴다.
 * 모든 문자열은 고정 배열이라 동적 할당/해제가 없다(NFR-01).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define CFG_MAX_ROUTES   8
#define CFG_MAX_BACKENDS 4
#define CFG_PREFIX_LEN   128
#define CFG_ADDR_LEN     64
#define CFG_PATH_LEN     256

typedef struct {
    char prefix[CFG_PREFIX_LEN];
    char backends[CFG_MAX_BACKENDS][CFG_ADDR_LEN];
    int backend_count;
} cfg_route_t;

typedef struct {
    char port[16];
    char doc_root[CFG_PATH_LEN];
    char config_file[CFG_PATH_LEN];
    int workers;
    int keepalive_max;
    int backend_timeout_sec;
    int cdn_ttl_sec;
    cfg_route_t routes[CFG_MAX_ROUTES];
    int route_count;
} server_config_t;

/* argc/argv에서 설정을 읽어 cfg를 채운다. 실패 -1, 성공 0. */
int config_load(int argc, char **argv, server_config_t *cfg);

/* prefix와 가장 오래 일치하는 라우트 인덱스. 없으면 -1. */
int config_match_route(const server_config_t *cfg, const char *uri, size_t uri_len);

#endif /* CONFIG_H */
