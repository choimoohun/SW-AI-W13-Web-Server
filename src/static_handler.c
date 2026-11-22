/*
 * static_handler.c — 요청 URI를 doc_root 하위 정적 파일 응답으로 처리. (오늘 범위 핵심)
 *
 * [경로 탈출 방어 — 고정 알고리즘] (verify/DESIGN_REVIEW.md §3)
 *  1) query/fragment 제거.
 *  2) 퍼센트 디코딩(1회). 제어문자/NUL/잘못된 %XX 는 400.
 *  3) 경로 '컴포넌트 단위'로 ".." 거부 → 403. ("/a..b" 는 정상)
 *  4) doc_root 와 결합. doc_root 는 realpath 로 절대경로화.
 *  5) realpath(결합경로) → 경계 포함 검사: resolved==root 또는 root+"/" 시작. 아니면 403.
 *     (단순 prefix 검사는 "/www-evil" 통과 함정 — 경계 "/" 확인 필수. 심볼릭 링크도 realpath 가 푼다.)
 *     보안: 파일 경로 검사와 실제 열기 사이의 미세한 틈을 이용한 공격은 이번 단계에선 고려하지 않음.
 *  6) stat/open → errno 매핑: ENOENT→404, EACCES→403, 기타→500.
 *  - "/" → index.html. 디렉터리는 index.html 시도. 목록 노출은 안 함.
 *
 * [MIME] 확장자 대소문자 무시. html/css/js/json/png/jpg/jpeg/txt, 그 외 octet-stream.
 * [인계 계약] {status,mime,file_fd,size} 반환. 청크 전송은 http_response, fd 닫기는 connection.
 * 요구사항: FR-04, NFR-01.
 */

#include "static_handler.h"
#include <string.h>    /* strlen, strcmp, strncmp, strrchr, strcpy */
#include <strings.h>   /* strcasecmp */
#include <stdlib.h>    /* realpath */
#include <stdio.h>     /* snprintf */
#include <errno.h>
#include <fcntl.h>     /* open, O_RDONLY */
#include <sys/stat.h>  /* stat, S_ISDIR, S_ISREG */
#include <limits.h>    /* PATH_MAX */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *mime_from_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    const char *e = dot + 1;
    if (strcasecmp(e, "html") == 0 || strcasecmp(e, "htm") == 0) return "text/html";
    if (strcasecmp(e, "css") == 0)  return "text/css";
    if (strcasecmp(e, "js") == 0)   return "application/javascript";
    if (strcasecmp(e, "json") == 0) return "application/json";
    if (strcasecmp(e, "png") == 0)  return "image/png";
    if (strcasecmp(e, "jpg") == 0 || strcasecmp(e, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(e, "txt") == 0)  return "text/plain";
    return "application/octet-stream";
}

static static_file_t err(int status)
{
    static_file_t f;
    f.status = status;
    f.mime = NULL;
    f.file_fd = -1;
    f.size = 0;
    return f;
}

static_file_t static_handler_resolve(const char *uri, size_t uri_len, const char *doc_root)
{
    /* 1) query/fragment 제거 */
    size_t pathlen = uri_len;
    for (size_t i = 0; i < uri_len; i++) {
        if (uri[i] == '?' || uri[i] == '#') { pathlen = i; break; }
    }

    /* 2) 퍼센트 디코딩(1회) → path */
    char path[2048 + 1];
    size_t plen = 0;
    for (size_t i = 0; i < pathlen; i++) {
        char c;
        if (uri[i] == '%') {
            if (i + 2 >= pathlen) return err(400);
            int hi = hexval(uri[i+1]), lo = hexval(uri[i+2]);
            if (hi < 0 || lo < 0) return err(400);
            c = (char)(hi * 16 + lo);
            if (c == '\0' || (unsigned char)c < 0x20) return err(400);
            i += 2;
        } else {
            c = uri[i];
        }
        if (plen >= sizeof(path) - 1) return err(414);
        path[plen++] = c;
    }
    path[plen] = '\0';
    if (plen == 0 || path[0] != '/') return err(400);

    /* 3) 컴포넌트 단위 ".." 거부 */
    const char *s = path;
    while (*s) {
        while (*s == '/') s++;
        const char *e = s;
        while (*e && *e != '/') e++;
        if (e - s == 2 && s[0] == '.' && s[1] == '.') return err(403);
        s = e;
    }

    /* "/" 또는 끝이 "/" 면 index.html */
    if (plen == 1 || path[plen-1] == '/') {
        if (plen + strlen("index.html") >= sizeof(path)) return err(414);
        strcpy(path + plen, "index.html");
        plen = strlen(path);
    }

    /* 4) doc_root 절대경로화 + 결합 */
    char root[PATH_MAX];
    if (!realpath(doc_root, root)) return err(500);
    char joined[PATH_MAX + 2048 + 2];
    int jn = snprintf(joined, sizeof(joined), "%s%s", root, path);
    if (jn < 0 || (size_t)jn >= sizeof(joined)) return err(414);

    /* 5) realpath + 경계 포함 검사 */
    char resolved[PATH_MAX];
    if (!realpath(joined, resolved)) {
        return err(errno == ENOENT ? 404 : errno == EACCES ? 403 : 500);
    }
    size_t rootlen = strlen(root);
    int inside = (strcmp(resolved, root) == 0) ||
                 (strncmp(resolved, root, rootlen) == 0 && resolved[rootlen] == '/');
    if (!inside) return err(403);

    /* 6) stat/open. 디렉터리면 index.html 시도 */
    char final[PATH_MAX];
    strcpy(final, resolved);
    struct stat st;
    if (stat(final, &st) < 0) {
        return err(errno == EACCES ? 403 : errno == ENOENT ? 404 : 500);
    }
    if (S_ISDIR(st.st_mode)) {
        int fn = snprintf(final, sizeof(final), "%s/index.html", resolved);
        if (fn < 0 || (size_t)fn >= sizeof(final)) return err(500);
        if (stat(final, &st) < 0) {
            return err(errno == EACCES ? 403 : errno == ENOENT ? 404 : 500);
        }
    }
    if (!S_ISREG(st.st_mode)) return err(403);

    int fd = open(final, O_RDONLY);
    if (fd < 0) return err(errno == EACCES ? 403 : 500);

    static_file_t f;
    f.status = 200;
    f.mime = mime_from_ext(final);
    f.file_fd = fd;
    f.size = (long)st.st_size;
    return f;
}
