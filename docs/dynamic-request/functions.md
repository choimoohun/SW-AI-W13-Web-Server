# 동적 요청 처리 함수 명세

[design.md](./design.md)의 `cgi.c`에 들어가는 함수의 역할, 입력, 출력입니다.

## cgi.c

| 함수 | 설명 | 입력 | 출력 |
| --- | --- | --- | --- |
| `int cgi_parse_output(char *out, size_t len, const char **type, size_t *head_len)` | CGI 출력에서 빈 줄까지의 헤더를 줄 단위로 읽어 `Content-Type` 값을 찾습니다. 헤더 이름은 대소문자를 구분하지 않고, 값 앞의 공백과 탭은 건너뜁니다. 줄 끝은 `\r\n`과 `\n`을 모두 받습니다. 소켓과 프로세스를 쓰지 않는 순수 함수입니다. | `out`, `len`: CGI 출력 전체 | 성공 0, `*type`은 `out` 안의 NUL로 끝낸 값, `*head_len`은 본문 시작 위치. 빈 줄이 없거나 `Content-Type`이 없거나, 값이 비었거나 `\r`이 섞이거나 `CGI_MAX_CONTENT_TYPE`(256)바이트 이상이면 -1. 헤더 줄의 줄 끝을 NUL로 바꿈 |
| `int cgi_serve(int fd, const char *path, const http_request *req)` | [CGI 실행 흐름](./design.md#cgi-실행-흐름)대로 프로그램을 실행하고 응답을 보냅니다. | `fd`: 연결 소켓. `path`: `static_resolve_path()`의 결과. `req`: 메서드, URI, 본문 | 응답을 끝까지 보냈으면 0, 송신 실패면 -1. 자식은 반환 전에 회수, 파이프와 임시 파일은 닫음. `fd`는 닫지 않음 |

### cgi_serve의 상태 코드

| 상황 | 상태 |
| --- | --- |
| `stat()` 실패: `ENOENT`, `ENOTDIR` / `EACCES` / `ENAMETOOLONG` / 그 밖 | 404 / 403 / 400 / 500 |
| 일반 파일이 아님, `access(X_OK)` 실패 | 403 |
| 출력 버퍼 할당, 임시 파일, `pipe()`, `fork()` 실패 | 500 |
| 5초 동안 출력 없음, 출력 1MiB 초과, EOF 뒤 5초 안에 끝나지 않음 | 500. 프로세스 그룹을 `SIGKILL` |
| 0이 아닌 종료 코드, 신호로 종료, `execve()` 실패(127) | 500 |
| 출력에 빈 줄이 없음, `Content-Type`이 없거나 비었거나 `\r`이 섞이거나 256바이트 이상 | 500 |
| 0으로 종료, 형식이 맞음 | 200. `Content-Type`은 CGI 값, `Content-Length`는 빈 줄 뒤 바이트 수 |

### static 함수

| 함수 | 역할 |
| --- | --- |
| `check_program(path)` | 실행할 수 있으면 0, 아니면 위 표의 상태 코드 |
| `body_file(req)` | 본문이 없으면 `/dev/null`, 있으면 본문을 쓴 이름 없는 임시 파일을 처음으로 되감아 반환. 실패면 -1 |
| `wait_child(pid)` | 최대 5초 동안 `waitpid(WNOHANG)`으로 회수. 0으로 끝났으면 0, 아니면(시간 초과 시 그룹째 `SIGKILL` 후 회수) -1 |
| `read_output(fd, buf, size)` | `poll()`과 `read()`로 EOF까지 읽어 길이 반환. 시간 초과, 오류, `size`를 다 채우면 -1 |
| `run_program(path, req, out, size)` | fork, exec, 출력 수집, `wait_child()`. 0으로 끝났으면 출력 길이, 아니면 -1 |

`cgi_serve()`는 버퍼를 `CGI_MAX_OUTPUT + 1`바이트로 잡아, `read_output()`이 버퍼를 다 채우면 상한을 넘은 것으로 봅니다.

## static_file.c에 추가한 공개 함수

| 함수 | 설명 | 출력 |
| --- | --- | --- |
| `int static_errno_status(int err)` | 4단계의 `open_status()`를 이름만 바꿔 공개했습니다. `static_serve()`와 `check_program()`이 함께 씁니다. | `ENOENT`, `ENOTDIR` 404, `EACCES` 403, `ENAMETOOLONG` 400, 그 밖 500 |
