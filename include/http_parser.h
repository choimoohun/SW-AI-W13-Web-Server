/*
 * http_parser.h — 요청 필드 + 파싱 결과/상태 선언.
 *
 * [요청 구조에 담을 것] 메서드, URI(포인터+길이), 버전, Host, Content-Length, Connection,
 * 본문 범위(포인터+길이). 입력은 포인터+길이, NUL 종료 가정 안 함. 입력 버퍼 수정 안 함.
 *
 * [결과 상태] COMPLETE / NEED_MORE / ERROR(상태코드) 셋을 구분해 connection에 반환.
 * [제한] URI 2048, 헤더 8192, 본문 1MiB. HTTP/1.1 Host 누락→400. (상세 오류표는 .c 주석)
 * 요구사항: FR-02.
 *
 * 아래 타입/선언은 제안 인터페이스다. 구현하며 조정 가능.
 */
#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>   /* size_t */

/* 파싱 결과 상태 */
typedef enum {
    PARSE_COMPLETE,    /* 요청 완성 → 처리로 진행 */
    PARSE_NEED_MORE,   /* 데이터 부족 → 더 읽어야 함 */
    PARSE_ERROR        /* 잘못된 요청 → req->err_status 로 응답 */
} parse_status_t;

/* 파싱된 요청. 필드는 입력 버퍼 내부를 가리키는 포인터+길이(복사 아님, NUL 가정 안 함). */
typedef struct {
    const char *method;      size_t method_len;    /* "GET" / "POST" */
    const char *uri;         size_t uri_len;       /* 요청 URI */
    int         http_minor;                        /* 0 또는 1 (HTTP/1.0 / 1.1) */
    const char *host;        size_t host_len;      /* Host 헤더 값, 없으면 NULL */
    long        content_length;                    /* Content-Length, 없으면 -1 */
    int         connection_close;                  /* Connection: close 이면 1 */
    const char *body;        size_t body_len;      /* 본문 시작/길이 */
    int         err_status;                        /* PARSE_ERROR 일 때 HTTP 상태코드 */
    size_t      consumed;                          /* COMPLETE일 때 이 요청이 차지한 총 바이트(keep-alive용) */
} http_request_t;

/* buf[0..len) 를 파싱해 req 에 채운다. 입력 버퍼는 수정하지 않는다.
 * 반환: COMPLETE / NEED_MORE / ERROR. */
parse_status_t http_parser_parse(const char *buf, size_t len, http_request_t *req);

#endif /* HTTP_PARSER_H */
