/*
 * http_response.h — 응답 인터페이스.
 *
 * [계약] 본문은 바이너리 가능. send 부분 성공 → send_all 반복. Content-Length=바이트 수.
 * 큰 파일은 청크 전송(전체 메모리 적재 금지). file_fd 닫기는 호출자(connection).
 * 요구사항: FR-03, FR-04.
 */
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

/* 오류 응답(빈 본문, Content-Length: 0) 전송. */
void http_response_send_error(int client_fd, int status);

/* 파일 응답(200): 헤더 + 청크 본문 전송. file_fd 는 닫지 않는다(connection 이 닫음). */
void http_response_send_file(int client_fd, const char *mime, int file_fd, long size);

#endif /* HTTP_RESPONSE_H */
