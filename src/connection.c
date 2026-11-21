/*
 * connection.c — 연결 하나의 수명과 요청/응답 흐름. (마일스톤3~4: 정적 파일 서빙)
 *
 * [핵심 원리]
 * - TCP read 한 번 ≠ 요청 하나. 누적 버퍼 + 파서가 COMPLETE/NEED_MORE/ERROR 판단까지 반복.
 * - Keep-Alive 미지원: Connection: close → 요청 1건 처리 후 닫는다.
 * - 타임아웃(NFR-03): SO_RCVTIMEO/SO_SNDTIMEO 5초.
 * - COMPLETE(GET) → static_handler 로 파일 해석 → http_response 로 전송 → file_fd 닫기.
 * - 청소는 단일 지점: 마지막에 close(client_fd) 한 번.
 * 요구사항: FR-02~04, NFR-01, NFR-03.
 */

#include "connection.h"
#include "http_parser.h"
#include "static_handler.h"
#include "http_response.h"
#include <unistd.h>      /* read, close */
#include <sys/socket.h>  /* setsockopt, SOL_SOCKET, SO_RCVTIMEO, SO_SNDTIMEO */
#include <sys/time.h>    /* struct timeval */
#include <errno.h>
#include <string.h>      /* memcmp */

void connection_handle(int client_fd, const char *doc_root)
{
    /* 읽기/쓰기 타임아웃 5초 (NFR-03) */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char buf[8192];
    size_t used = 0;
    http_request_t req;

    for (;;) {
        parse_status_t st = http_parser_parse(buf, used, &req);

        if (st == PARSE_COMPLETE) {
            /* 정적 서빙은 GET 의미론. POST 등 다른 메서드는 405. */
            int is_get = (req.method_len == 3 && memcmp(req.method, "GET", 3) == 0);
            if (!is_get) {
                http_response_send_error(client_fd, 405);
                break;
            }
            static_file_t f = static_handler_resolve(req.uri, req.uri_len, doc_root);
            if (f.status == 200) {
                http_response_send_file(client_fd, f.mime, f.file_fd, f.size);
                close(f.file_fd);   /* file_fd 소유권: connection 이 닫는다 */
            } else {
                http_response_send_error(client_fd, f.status);
            }
            break;
        }
        if (st == PARSE_ERROR) {
            http_response_send_error(client_fd, req.err_status);
            break;
        }

        /* PARSE_NEED_MORE: 더 읽는다. 버퍼가 가득 찼으면 헤더 과대. */
        if (used >= sizeof(buf)) {
            http_response_send_error(client_fd, 431);
            break;
        }
        ssize_t n = read(client_fd, buf + used, sizeof(buf) - used);
        if (n > 0) { used += (size_t)n; continue; }
        if (n == 0) break;                 /* 클라이언트 EOF */
        if (errno == EINTR) continue;
        break;                             /* 타임아웃/오류 → 정리 */
    }

    close(client_fd);   /* 단일 정리 지점 */
}
