#ifndef CGI_H
#define CGI_H

#include <stddef.h>

#include "http_request.h"

#define CGI_TIMEOUT_SEC 5             /* CGI 출력이 이 시간 동안 한 바이트도 없으면 죽이고 500 */
#define CGI_MAX_OUTPUT (1024 * 1024) /* 헤더를 포함한 CGI 출력 상한. 넘으면 죽이고 500 */

/*
 * CGI 출력 out(len바이트)에서 빈 줄까지의 헤더를 읽어 Content-Type 값을 찾습니다.
 * 줄 끝은 "\r\n"과 "\n"을 모두 받습니다. 성공하면 0을 반환하고, *type에 out 안의 NUL로 끝낸 값을,
 * *head_len에 빈 줄까지의 바이트 수(본문 시작 위치)를 저장합니다.
 * 빈 줄이 없거나 Content-Type이 없으면 -1을 반환합니다. out의 헤더 줄 끝을 NUL로 바꿉니다. 할당하는 자원은 없습니다.
 */
int cgi_parse_output(char *out, size_t len, const char **type, size_t *head_len);

/*
 * path의 CGI 프로그램을 실행해 그 결과를 fd로 응답합니다. 환경 변수 REQUEST_METHOD, QUERY_STRING(uri의 '?' 뒤),
 * CONTENT_LENGTH를 넘기고, 요청 본문을 표준 입력으로 줍니다. 프로그램이 0으로 끝나고 출력에 Content-Type이 있으면
 * 200, 프로그램이 없으면 404, 실행 권한이 없거나 일반 파일이 아니면 403, 그 밖의 실패(비정상 종료, 시간 초과,
 * 출력 초과, 형식 오류)는 500을 보냅니다. 응답을 끝까지 보냈으면 0, 송신에 실패하면 -1(errno는 EPIPE 등)을 반환합니다.
 * 자식 프로세스는 반환 전에 회수하고, 만든 파이프와 임시 파일은 닫습니다. fd는 닫지 않습니다.
 */
int cgi_serve(int fd, const char *path, const http_request *req);

#endif /* CGI_H */
