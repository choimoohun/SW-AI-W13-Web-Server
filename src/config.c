/*
 * config.c — 기본값 → conf 파일 → CLI 순으로 설정을 덮어쓴다.
 *
 * conf 형식(key=value, '#' 주석):
 *   port = 8080
 *   doc_root = ./www
 *   workers = 4
 *   keepalive_max = 100
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>   /* strtol */
#include <string.h>   /* memcpy, strcmp, strchr */

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
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            cfg->workers = (int)strtol(argv[++i], NULL, 10);
        }
    }

    if (cfg->workers < 1 || cfg->workers > 16) cfg->workers = 4;
    if (cfg->keepalive_max < 1) cfg->keepalive_max = 100;
    return 0;
}
