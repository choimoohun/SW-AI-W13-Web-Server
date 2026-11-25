/*
 * cgi.c - CGI 프로그램 실행 (CS:APP3e 11.5.4의 serve_dynamic을 스레드 풀에 맞게 고침)
 * 설계: docs/dynamic-request/
 */
#include <poll.h>
#include <strings.h>
#include <time.h>

#include "cgi.h"
#include "http_response.h"
#include "static_file.h"

int cgi_parse_output(char *out, size_t len, const char **type, size_t *head_len)
{
    char *line = out, *end = out + len;

    *type = NULL;
    while (line < end) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        if (nl == NULL)
            return -1;
        char *eol = (nl > line && nl[-1] == '\r') ? nl - 1 : nl;
        if (eol == line) { /* 빈 줄: 헤더 끝 */
            *head_len = (size_t)(nl + 1 - out);
            /* 빈 값이나 줄 중간의 '\r'은 응답 헤더를 깨뜨리고, 너무 긴 값은 http_send_head()의 버퍼를 넘칩니다. */
            if (*type == NULL || **type == '\0' || strchr(*type, '\r') != NULL ||
                strlen(*type) >= CGI_MAX_CONTENT_TYPE)
                return -1;
            return 0;
        }
        *eol = '\0';
        if (strncasecmp(line, "Content-Type:", 13) == 0)
            *type = line + 13 + strspn(line + 13, " \t");
        line = nl + 1;
    }
    return -1;
}

/* 실행할 수 있는 프로그램이면 0, 아니면 보낼 상태 코드를 반환합니다. */
static int check_program(const char *path)
{
    struct stat sb;
    if (stat(path, &sb) < 0)
        return static_errno_status(errno);
    if (!S_ISREG(sb.st_mode) || access(path, X_OK) < 0)
        return 403;
    return 0;
}

/*
 * CGI의 표준 입력으로 줄 fd를 반환합니다. 본문이 없으면 /dev/null, 있으면 본문을 담아 처음으로 되감은 임시 파일.
 * 실패하면 -1. 파이프로 넣으면 CGI가 본문을 읽기 전에 출력을 64KiB 넘게 쓸 때 서로를 기다리므로 파일로 줍니다.
 */
static int body_file(const http_request *req)
{
    if (req->content_length == 0)
        return open("/dev/null", O_RDONLY | O_CLOEXEC);

    char name[] = "/tmp/httpd-cgi-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0)
        return -1;
    unlink(name); /* 이름을 지워 두면 마지막 close에서 파일이 사라집니다. */
    fcntl(fd, F_SETFD, FD_CLOEXEC); /* shortcut: 아래 pipe()와 같은 틈이 있음 */
    if (rio_writen(fd, req->body, req->content_length) != (ssize_t)req->content_length ||
        lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* EOF까지 읽어 바이트 수를 반환합니다. 시간 초과, 읽기 오류, size를 채우면 -1. */
static ssize_t read_output(int fd, char *buf, size_t size)
{
    size_t len = 0;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};

    while (len < size) {
        int ready = poll(&pfd, 1, CGI_TIMEOUT_SEC * 1000);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            return -1;
        ssize_t n = read(fd, buf + len, size - len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -1;
        if (n == 0)
            return (ssize_t)len;
        len += (size_t)n;
    }
    return -1;
}

/*
 * 자식이 끝나기를 CGI_TIMEOUT_SEC까지 기다려 회수합니다. 0으로 끝났으면 0, 아니면 -1.
 * 출력을 닫고 계속 도는 CGI가 워커를 붙잡지 않게, 시간이 지나면 프로세스 그룹째 죽입니다.
 * 다른 워커의 자식을 가로채지 않게 pid를 지정합니다.
 */
static int wait_child(pid_t pid)
{
    int status;
    for (int tick = 0; tick < CGI_TIMEOUT_SEC * 100; tick++) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
        if (r < 0 && errno != EINTR)
            return -1;
        nanosleep(&(struct timespec){.tv_nsec = 10 * 1000 * 1000}, NULL); /* 10ms */
    }
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return -1;
}

/* 프로그램을 실행해 표준 출력을 out에 모읍니다. 0으로 끝났으면 출력 길이, 아니면 -1을 반환합니다. */
static ssize_t run_program(const char *path, const http_request *req, char *out, size_t size)
{
    /* 여러 스레드가 있는 프로세스의 자식은 exec 전에 malloc을 부르면 안 되므로 fork 전에 모두 만듭니다. */
    char method[32], query[MAXLINE + 16], length[48];
    const char *q = strchr(req->uri, '?');
    snprintf(method, sizeof method, "REQUEST_METHOD=%s", req->method);
    snprintf(query, sizeof query, "QUERY_STRING=%s", q ? q + 1 : "");
    snprintf(length, sizeof length, "CONTENT_LENGTH=%zu", req->content_length);
    char *argv[] = {(char *)path, NULL};
    char *envp[] = {method, query, length, NULL};
    sigset_t no_signals;
    sigemptyset(&no_signals);

    int p[2];
    int in = body_file(req);
    if (in < 0)
        return -1;
    if (pipe(p) < 0) {
        close(in);
        return -1;
    }
    /* 다른 워커가 fork한 CGI에 이 파이프가 새지 않게 합니다.
       shortcut: pipe()와 fcntl() 사이에 다른 워커가 fork하면 샘. 그 CGI가 끝날 때까지 EOF가 늦어질 뿐이라 둠 */
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid == 0) { /* 자식: async-signal-safe 함수만 부릅니다. */
        dup2(in, STDIN_FILENO);
        dup2(p[1], STDOUT_FILENO);
        setpgid(0, 0); /* 시간 초과 때 CGI가 만든 손자 프로세스까지 한 번에 죽이려고 새 그룹을 만듭니다. */
        sigprocmask(SIG_SETMASK, &no_signals, NULL); /* 워커가 막아 둔 SIGINT, SIGTERM을 풉니다. 자식은 스레드가 하나입니다. */
        signal(SIGPIPE, SIG_DFL);                    /* 서버의 SIG_IGN은 exec 뒤에도 남습니다. */
        execve(path, argv, envp);
        _exit(127);
    }
    close(in);
    close(p[1]); /* 쓰는 쪽을 닫아야 CGI가 끝날 때 EOF가 옵니다. */
    if (pid < 0) {
        close(p[0]);
        return -1;
    }
    setpgid(pid, pid); /* 자식과 같은 일을 부모도 해 두면 kill(-pid) 전에 그룹이 있다는 것이 보장됩니다. */

    ssize_t len = read_output(p[0], out, size);
    close(p[0]);
    if (len < 0)
        kill(-pid, SIGKILL);
    return (wait_child(pid) == 0 && len >= 0) ? len : -1;
}

int cgi_serve(int fd, const char *path, const http_request *req)
{
    int status = check_program(path);
    if (status != 0)
        return http_send_error(fd, status);

    /* 200을 먼저 보내면 CGI가 실패해도 되돌릴 수 없으므로, 출력을 모두 받은 뒤 상태를 정합니다.
       버퍼를 1바이트 크게 잡아 꽉 차면 상한을 넘은 것으로 봅니다. */
    char *out = malloc(CGI_MAX_OUTPUT + 1);
    ssize_t len = out ? run_program(path, req, out, CGI_MAX_OUTPUT + 1) : -1;
    const char *type;
    size_t head_len;
    int rc;
    if (len < 0 || cgi_parse_output(out, (size_t)len, &type, &head_len) < 0) {
        rc = http_send_error(fd, 500);
    } else {
        size_t body_len = (size_t)len - head_len;
        rc = (http_send_head(fd, 200, type, body_len) == 0 &&
              rio_writen(fd, out + head_len, body_len) == (ssize_t)body_len)
                 ? 0
                 : -1;
    }
    free(out);
    return rc;
}
