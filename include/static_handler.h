/*
 * static_handler.h — 정적 파일 인터페이스.
 *
 * [입력] 파싱된 URI + doc_root.
 * [출력] 파일 응답 descriptor {status,mime,file_fd,size} 또는 오류 상태(400/403/404/414/500).
 * [계약] 경로 탈출 거부(컴포넌트 단위 ".." + realpath 경계 검사). "/"→index.html.
 * MIME 매핑 반영. 본문은 임의 바이트. 청크 전송은 http_response, file_fd 닫기는 connection.
 * 요구사항: FR-04.
 */
#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

#include <stddef.h>   /* size_t */

typedef struct {
    int         status;    /* 200 또는 오류 코드 */
    const char *mime;      /* 200 일 때 Content-Type */
    int         file_fd;   /* 200 일 때 열린 파일 fd, 아니면 -1 (닫기는 connection) */
    long        size;      /* 파일 크기 = Content-Length */
} static_file_t;

/* uri[0..uri_len) 를 doc_root 하위 파일로 해석. 탈출 차단, "/"→index.html.
 * 성공 시 file_fd 열린 채 반환(호출자가 닫음). 실패 시 status=오류, file_fd=-1. */
static_file_t static_handler_resolve(const char *uri, size_t uri_len, const char *doc_root);

#endif /* STATIC_HANDLER_H */
