/*
 * http_response.h — 응답 인터페이스.
 *
 * [계약] 본문은 바이너리 가능. send 부분 성공 → send_all 반복. Content-Length=바이트 수.
 * 큰 파일은 청크 전송(전체 메모리 적재 금지). file_fd 닫기는 호출자(connection).
 * 요구사항: FR-03, FR-04.
 */
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>   /* size_t */

/* 오류 응답(빈 본문, Content-Length: 0) 전송. */
void http_response_send_error(int client_fd, int status);

/* 파일 응답(200): 헤더 + 청크 본문 전송. file_fd 는 닫지 않는다(connection 이 닫음). */
void http_response_send_file(int client_fd, const char *mime, int file_fd, long size);

/* JSON 응답: Content-Type application/json; charset=utf-8, Content-Length=len.
 * body는 바이트 길이를 넘긴다(strlen 금지). */
void http_response_send_json(int client_fd, int status, const char *body, size_t len);

/* 논블로킹 이벤트 루프용 직렬화: 소켓에 쓰지 않고 out에 HTTP 응답 바이트를 만든다.
 * keep_alive=1이면 Connection: keep-alive 헤더를 넣는다.
 * 반환=바이트 수, 버퍼 부족/실패 시 0. */
size_t http_response_build_json(char *out, size_t cap, int status,
                                const char *body, size_t len, int keep_alive);
size_t http_response_build_error(char *out, size_t cap, int status, int keep_alive);
size_t http_response_build_file(char *out, size_t cap, const char *mime,
                                long size, int keep_alive, const char *extra_headers);

/* keep-alive 유휴 시간(초). Keep-Alive 응답 헤더와 worker.c의 유휴 정리가 같은 값을 쓴다. */
#define HTTP_KEEPALIVE_TIMEOUT_SEC 5

#endif /* HTTP_RESPONSE_H */
