/*
 * proxy.h — 리버스 프록시 (FR-07).
 * 경로 prefix 게이트웨이 + 다중 백엔드 Round-Robin 로드밸런서.
 *
 * 워커 이벤트 루프를 블로킹하지 않도록 전용 프록시 스레드가 큐를 처리한다.
 * 워커는 요청을 큐에 넘기고 연결 소유권을 넘긴 뒤(duplicate fd) 즉시 루프로 복귀한다.
 */
#ifndef PROXY_H
#define PROXY_H

#include "config.h"
#include "http_parser.h"

/* 프록시 스레드 시작/종료. main에서 워커 풀 전/후에 호출. */
int  proxy_start(void);
void proxy_stop(void);

/* 요청을 프록시 큐에 넣는다. 성공 0, 일반 실패 -1(502), 큐 포화 -2(503).
 * 성공하면 원본 fd의 소유권은 프록시로 넘어가므로(evict된 연결은 워커가 정리) 호출자가
 * c->proxied를 설정하고 연결을 닫아야 한다. 요청 필드는 내부로 복사한다. */
int proxy_submit(int client_fd, const http_request_t *req,
                 const server_config_t *cfg, int route_index,
                 const char *client_ip, int worker_id);

#endif /* PROXY_H */
