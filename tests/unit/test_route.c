/*
 * test_route.c — 라우팅/rewrite 단위 테스트 (순수 함수, IO 없음).
 * 빌드: cc $(CFLAGS) -o test_route tests/unit/test_route.c src/config.c
 */
#include "config.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static void add_route(server_config_t *cfg, const char *prefix, const char *rewrite)
{
    cfg_route_t *r = &cfg->routes[cfg->route_count];
    snprintf(r->prefix, sizeof(r->prefix), "%s", prefix);
    snprintf(r->rewrite, sizeof(r->rewrite), "%s", rewrite);
    r->backend_count = 0;
    cfg->route_count++;
}

int main(void)
{
    server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* 최장 prefix가 이겨야 한다. */
    add_route(&cfg, "/api", "");
    add_route(&cfg, "/api/orders", "");
    CHECK(config_match_route(&cfg, "/api/orders/test", 16) == 1, "longest prefix wins");
    CHECK(config_match_route(&cfg, "/api/users", 11) == 0, "shorter prefix fallback");
    CHECK(config_match_route(&cfg, "/", 1) == -1, "no match -> -1");

    /* rewrite: prefix 뒤 경로를 rewrite로 교체. */
    {
        char out[256];
        size_t n = config_apply_rewrite(&cfg.routes[1], "/api/orders/test", 16,
                                        out, sizeof(out));
        CHECK(n == 16 && memcmp(out, "/api/orders/test", 16) == 0, "rewrite empty -> original uri");
    }

    {
        char out[256];
        cfg_route_t *r = &cfg.routes[1];
        snprintf(r->rewrite, sizeof(r->rewrite), "/v1/orders");
        size_t n = config_apply_rewrite(r, "/api/orders/test", 16, out, sizeof(out));
        CHECK(n == 15 && memcmp(out, "/v1/orders/test", 15) == 0, "rewrite replaces prefix");
    }

    {
        /* prefix보다 짧은 URI는 빈 rest로 처리. */
        char out[256];
        size_t n = config_apply_rewrite(&cfg.routes[1], "/api/orders", 11, out, sizeof(out));
        CHECK(n == 10 && memcmp(out, "/v1/orders", 10) == 0, "uri equals prefix -> rewrite only");
    }

    {
        /* 출력 버퍼가 작으면 잘린 뒤 NUL로 끝나야 한다. */
        char out[8];
        size_t n = config_apply_rewrite(&cfg.routes[1], "/api/orders/test", 16, out, sizeof(out));
        CHECK(n == 7 && memcmp(out, "/v1/ord", 7) == 0, "truncate to buffer");
    }

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("test_route: all checks passed\n");
    return 0;
}
