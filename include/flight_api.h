/*
 * flight_api.h — /api/... 요청 처리 인터페이스. (STEP6 스캐폴딩)
 *
 * [호출 계약]
 *   connection.c가 파싱 완료 후 URI가 "/api/"로 시작하면 이 함수를 호출한다.
 *   반환값 0 = 응답까지 전송하고 처리 완료.
 *   반환값 -1 = 이 모듈이 처리하지 않음 → 호출자가 정적 파일 경로로 계속 진행.
 *   스캐폴딩 상태에서는 항상 -1을 반환하므로 connection 배선 전에는 동작 변화 없음.
 *
 * [의존] http_request_t의 포인터+길이 필드는 입력 버퍼를 가리킨다. 복사해서 쓸 것.
 */
#ifndef FLIGHT_API_H
#define FLIGHT_API_H

#include <stddef.h>   /* size_t */

#include "http_parser.h"   /* http_request_t */

/* API 응답. 소켓에 쓰지 않고 버퍼로 돌려준다(논블로킹 이벤트 루프와 분리). */
typedef struct {
    int status;                 /* HTTP 상태코드 */
    char body[16384];           /* JSON 본문 */
    size_t len;                 /* body 바이트 길이 */
} api_response_t;

/* URI가 "/api/"로 시작하는지 확인. 접두사만 보고 판단한다. */
int flight_api_is_api_uri(const char *uri, size_t uri_len);

/* 요청을 처리해 out에 응답을 만든다. 반환 0=처리함, -1=API 경로 아님. */
int flight_api_build(const http_request_t *req, api_response_t *out);

/* /health, /echo 같은 내장 동적 엔드포인트(FR-05). 처리 시 0. */
int flight_api_builtin(const http_request_t *req, api_response_t *out);

#endif /* FLIGHT_API_H */
