/*
 * config.c — 기본값 → conf 파일 → CLI 순으로 설정을 덮어쓴다.
 *
 * conf 형식(key=value, '#' 주석):
 *   port = 8080
 *   doc_root = ./www
 *   workers = 4
 *   keepalive_max = 100
 *   backend_timeout = 5
 *   cdn_ttl = 60
 *   route = /api/cache/|127.0.0.1:8000,127.0.0.1:8001
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>   /* strtol */
#include <string.h>   /* strncpy, strcmp, strchr */

static void trim(char *s)
{
    size_t n = strlen(s);
    char *p;

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                     s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
    /* 앞쪽 공백은 포인터만 옮기면 호출자가 여전히 원본을 쓰므로 실제로 당긴다. */
    p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void set_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void defaults(server_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    set_str(cfg->port, sizeof(cfg->port), "8080");
    set_str(cfg->doc_root, sizeof(cfg->doc_root), "./www");
    set_str(cfg->config_file, sizeof(cfg->config_file), "conf/tcpserver.conf");
    cfg->workers = 4;
    cfg->keepalive_max = 100;
    cfg->backend_timeout_sec = 5;
    cfg->cdn_ttl_sec = 60;
}

static void parse_route(server_config_t *cfg, const char *value)
{
    char buf[512];
    char *bar;
    char *p;
    cfg_route_t *r;

    if (cfg->route_count >= CFG_MAX_ROUTES) return;
    set_str(buf, sizeof(buf), value);
    bar = strchr(buf, '|');
    if (bar == NULL) return;
    *bar = '\0';
    trim(buf);

    r = &cfg->routes[cfg->route_count];
    memset(r, 0, sizeof(*r));
    set_str(r->prefix, sizeof(r->prefix), buf);

    p = bar + 1;
    while (*p != '\0' && r->backend_count < CFG_MAX_BACKENDS) {
        char *comma = strchr(p, ',');
        if (comma != NULL) *comma = '\0';
        trim(p);
        if (*p != '\0') {
            set_str(r->backends[r->backend_count], CFG_ADDR_LEN, p);
            r->backend_count++;
        }
        if (comma == NULL) break;
        p = comma + 1;
    }
    if (r->backend_count > 0) cfg->route_count++;
}

static int load_file(server_config_t *cfg, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[1024];

    if (fp == NULL) return -1;   /* 설정 파일 없으면 기본값 사용 */
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;
        char *key;
        char *value;

        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        key = line;
        value = eq + 1;
        trim(key);
        trim(value);

        if (strcmp(key, "port") == 0) {
            set_str(cfg->port, sizeof(cfg->port), value);
        } else if (strcmp(key, "doc_root") == 0) {
            set_str(cfg->doc_root, sizeof(cfg->doc_root), value);
        } else if (strcmp(key, "workers") == 0) {
            cfg->workers = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "keepalive_max") == 0) {
            cfg->keepalive_max = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "backend_timeout") == 0) {
            cfg->backend_timeout_sec = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "cdn_ttl") == 0) {
            cfg->cdn_ttl_sec = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "route") == 0) {
            parse_route(cfg, value);
        }
    }
    fclose(fp);
    return 0;
}

int config_load(int argc, char **argv, server_config_t *cfg)
{
    defaults(cfg);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            set_str(cfg->config_file, sizeof(cfg->config_file), argv[++i]);
        }
    }
    (void)load_file(cfg, cfg->config_file);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            set_str(cfg->port, sizeof(cfg->port), argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            set_str(cfg->doc_root, sizeof(cfg->doc_root), argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            i++;   /* 이미 적용됨 */
        }
    }

    if (cfg->workers < 1 || cfg->workers > 16) cfg->workers = 4;
    if (cfg->keepalive_max < 1) cfg->keepalive_max = 100;
    if (cfg->backend_timeout_sec < 1) cfg->backend_timeout_sec = 5;
    if (cfg->cdn_ttl_sec < 1) cfg->cdn_ttl_sec = 60;
    return 0;
}

int config_match_route(const server_config_t *cfg, const char *uri, size_t uri_len)
{
    int best = -1;
    size_t best_len = 0;

    for (int i = 0; i < cfg->route_count; ++i) {
        size_t n = strlen(cfg->routes[i].prefix);
        if (uri_len >= n && memcmp(uri, cfg->routes[i].prefix, n) == 0 && n > best_len) {
            best = i;
            best_len = n;
        }
    }
    return best;
}
