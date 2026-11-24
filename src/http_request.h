#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <stddef.h>

#include "csapp.h"

#define HTTP_MAX_BODY (1024 * 1024)

enum {
    HTTP_READ_OK = 0,      /* 요청을 끝까지 읽음 */
    HTTP_READ_CLOSED = -1, /* EOF 또는 읽기 오류. 응답 없이 연결을 닫음 */
    HTTP_READ_BAD = 400,   /* 형식 오류나 크기 초과. 400을 보내고 연결을 닫음 */
    HTTP_READ_NOMEM = 500  /* 본문 버퍼 할당 실패. 500을 보내고 연결을 닫음 */
};

typedef struct {
    char method[16];
    char uri[MAXLINE];
    char version[16];
    char host[MAXLINE];    /* 없으면 빈 문자열 */
    char connection[32];   /* 없으면 빈 문자열 */
    size_t content_length; /* 없으면 0 */
    char *body;            /* content_length > 0이면 malloc, 아니면 NULL */
} http_request;

/*
 * rp에서 한 줄씩 읽어 빈 줄("\r\n")까지의 헤더 블록을 buf에 이어 붙이고 NUL로 끝냅니다.
 * 성공하면 HTTP_READ_OK를 반환하고 *len에 NUL을 뺀 바이트 수를 저장합니다.
 * 줄 끝 '\n' 앞에 '\r'이 없거나 헤더 블록이 size(끝 NUL 포함)에 들어가지 않으면 HTTP_READ_BAD,
 * 빈 줄 전에 EOF(줄 중간의 EOF 포함)나 읽기 오류가 나면 HTTP_READ_CLOSED를 반환합니다.
 * 빈 줄 뒤의 바이트는 rp의 내부 버퍼에 남겨 둡니다. 할당하는 자원은 없습니다.
 */
int http_read_head(rio_t *rp, char *buf, size_t size, size_t *len);

/*
 * http_read_head()가 만든 헤더 블록 buf(len바이트)를 파싱해 req의 body를 뺀 필드를 채웁니다.
 * 성공하면 0, 형식 오류나 크기 초과면 400, HTTP/1.0과 HTTP/1.1이 아닌 HTTP 버전이면 505를 반환합니다.
 * 실패하면 req의 필드 값은 정의되지 않습니다. 소켓을 쓰지 않고 할당하는 자원도 없습니다.
 */
int http_parse_head(const char *buf, size_t len, http_request *req);

/*
 * req->content_length가 0보다 크면 그만큼 malloc해 같은 rp에서 본문을 읽어 req->body에 둡니다.
 * 본문은 NUL로 끝나지 않습니다. 성공하면 HTTP_READ_OK, malloc 실패면 HTTP_READ_NOMEM,
 * 본문을 다 읽기 전에 EOF나 읽기 오류가 나면 HTTP_READ_CLOSED를 반환합니다.
 * 반환값과 관계없이 호출자는 http_request_free()로 req->body를 해제해야 합니다.
 */
int http_read_body(rio_t *rp, http_request *req);

/*
 * req->body를 해제하고 NULL로 둡니다. body가 NULL이어도 안전합니다. 반환값은 없습니다.
 */
void http_request_free(http_request *req);

#endif /* HTTP_REQUEST_H */
