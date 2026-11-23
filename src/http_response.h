#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>

#include "csapp.h"

/*
 * status의 상태 메시지(예: 404 -> "Not found")를 반환합니다.
 * 200, 400, 403, 404, 500, 501, 505가 아니면 NULL을 반환합니다. 반환값은 문자열 상수이므로 해제하지 않습니다.
 */
const char *http_status_text(int status);

/*
 * 응답 줄, Content-Type, Content-Length, Connection: close, 빈 줄을 buf(size바이트)에 NUL로 끝내 씁니다.
 * 성공하면 NUL을 뺀 바이트 수를 반환합니다. 상태 메시지가 없거나 size에 다 들어가지 않으면 -1을 반환하며,
 * 이때 buf의 내용은 정의되지 않습니다. 소켓을 쓰지 않고 할당하는 자원도 없습니다.
 */
int http_format_head(char *buf, size_t size, int status, const char *content_type, size_t content_length);

/*
 * http_format_head()로 만든 헤더를 fd로 보냅니다. 본문은 보내지 않습니다.
 * 성공하면 0, 형식 실패나 송신 실패면 -1을 반환합니다(송신 실패 시 errno는 rio_writen()이 남긴 값,
 * 예: EPIPE, ECONNRESET). fd는 닫지 않으며 할당하는 자원은 없습니다.
 */
int http_send_head(int fd, int status, const char *content_type, size_t content_length);

/*
 * status의 HTML 오류 본문을 만들어 text/html 헤더와 함께 fd로 보냅니다.
 * 성공하면 0, 상태 메시지가 없거나 송신에 실패하면 -1을 반환합니다. fd는 닫지 않으며 할당하는 자원은 없습니다.
 */
int http_send_error(int fd, int status);

#endif /* HTTP_RESPONSE_H */
