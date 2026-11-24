/*
 * config.h — 실행 설정. 기본값 → 설정 파일 → CLI 순으로 덮어쓴다.
 * 모든 문자열은 고정 배열이라 동적 할당/해제가 없다(NFR-01).
 */
#ifndef CONFIG_H
#define CONFIG_H

#define CFG_PATH_LEN 256

typedef struct {
    char port[16];
    char doc_root[CFG_PATH_LEN];
    char config_file[CFG_PATH_LEN];
    int workers;
    int keepalive_max;
} server_config_t;

/* argc/argv에서 설정을 읽어 cfg를 채운다. 실패 -1, 성공 0. */
int config_load(int argc, char **argv, server_config_t *cfg);

#endif /* CONFIG_H */
