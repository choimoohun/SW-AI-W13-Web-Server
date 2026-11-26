/* test_proxy.c — 프록시(라우트 선택·rewrite) 단위 테스트 (IO 없음).
 *
 *  - /api/payments/charge → /charge 로 경로 변경
 *  - 라운드로빈 순서: 설정 라우트의 백엔드 순서 A → B (런타임 RR 은 통합 테스트에서 확인)
 *  - 일치하는 route 없음
 *
 * 빌드: cc $(CFLAGS) -o test_proxy tests/unit/test_proxy.c src/config.c
 */
#include "config.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { printf("FAIL: %s\n", msg); failures++; }          \
    } while (0)

static void add_route(server_config_t *cfg, const char *prefix, const char *rewrite,
                       const char **backends, int n)
{
    cfg_route_t *r = &cfg->routes[cfg->route_count];
    snprintf(r->prefix, sizeof(r->prefix), "%s", prefix);
    snprintf(r->rewrite, sizeof(r->rewrite), "%s", rewrite);
    r->backend_count = n;
    for (int i = 0; i < n && i < CFG_MAX_BACKENDS; i++) {
        snprintf(r->backends[i], sizeof(r->backends[i]), "%s", backends[i]);
    }
    cfg->route_count++;
}

int main(void)
{
    server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    /* 결제 경로: prefix 뒤 경로를 rewrite("/")로 교체. */
    const char *pay_backends[] = { "127.0.0.1:8500" };
    add_route(&cfg, "/api/payments/", "/", pay_backends, 1);
    CHECK(config_match_route(&cfg, "/api/payments/charge", 20) == 0,
          "payments route matches");
    {
        char out[256];
        size_t n = config_apply_rewrite(&cfg.routes[0], "/api/payments/charge", 20,
                                        out, sizeof(out));
        CHECK(n == 7 && memcmp(out, "/charge", 7) == 0, "rewrite -> /charge");
    }

    /* 공항 정보 서비스 2대: 백엔드 순서가 RR 의 기초. */
    const char *air_backends[] = {
        "127.0.0.1:8601", "127.0.0.1:8602",
    };
    add_route(&cfg, "/api/airports/", "/", air_backends, 2);
    CHECK(config_match_route(&cfg, "/api/airports/ICN", 18) == 1,
          "airports route matches");
    CHECK(cfg.routes[1].backend_count == 2, "2 backends configured");
    CHECK(strcmp(cfg.routes[1].backends[0], "127.0.0.1:8601") == 0,
          "backend list order A then B");

    /* 일치하는 route 없음. */
    CHECK(config_match_route(&cfg, "/api/flights", 12) == -1,
          "no matching route -> -1");
    CHECK(config_match_route(&cfg, "/", 1) == -1, "root -> -1");
    CHECK(config_match_route(&cfg, "/api", 4) == -1, "short prefix -> -1");

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("test_proxy: all checks passed\n");
    return 0;
}
