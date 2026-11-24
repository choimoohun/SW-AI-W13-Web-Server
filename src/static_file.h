#ifndef STATIC_FILE_H
#define STATIC_FILE_H

#include <stddef.h>

#include "csapp.h"

/*
 * path의 확장자(마지막 '/' 뒤, 마지막 '.' 다음)로 MIME 타입을 찾습니다. 대소문자를 구분하지 않습니다.
 * 표에 없거나 확장자가 없으면 "application/octet-stream"을 반환합니다. NULL을 반환하지 않으며,
 * 반환값은 문자열 상수이므로 해제하지 않습니다.
 */
const char *mime_type(const char *path);

/*
 * '/'로 시작하는 uri를 root 아래 디스크 경로로 바꿔 path(size바이트)에 NUL로 끝내 씁니다.
 * '?' 뒤는 버리고, '/'로 끝나면 "index.html"을 붙입니다.
 * 성공하면 0, 경로 조각 중 정확히 ".."이 있으면 403, 결과가 size에 들어가지 않으면 400을 반환합니다.
 * 실패하면 path의 내용은 정의되지 않습니다. 파일 시스템에 접근하지 않고 할당하는 자원도 없습니다.
 */
int static_resolve_path(const char *root, const char *uri, char *path, size_t size);

/*
 * path를 열어 일반 파일이면 200 헤더와 파일 내용을 fd로 보내고, 아니면 상태 코드(404, 403, 400, 500)에
 * 맞는 오류 응답을 보냅니다. 본문은 fstat()으로 얻은 크기(Content-Length)만큼만 보냅니다.
 * 응답을 끝까지 보냈으면 0, 송신 실패(errno는 EPIPE 등), 본문 전송 중 read() 실패, 그 크기만큼 읽기 전의
 * EOF면 -1을 반환합니다. 연 파일은 반환 전에 닫고, fd는 닫지 않습니다.
 */
int static_serve(int fd, const char *path);

#endif /* STATIC_FILE_H */
