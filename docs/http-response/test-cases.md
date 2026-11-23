# HTTP 응답 생성 테스트 케이스

[design.md](./design.md)대로 만든 응답 생성이 맞는지 확인하는 최소 테스트입니다. 기대 결과는 [functions.md](./functions.md)를 따릅니다.

## 단위 테스트

`tests/test_http_response.c`에 작성하고 CTest에 `test_http_response`로 등록합니다. 빌드와 등록 방법은 [정적 파일 서빙 설계의 빌드 설계](../static-file-serving/design.md#빌드-설계)에 있습니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| R01 | `http_format_head(buf, MAXLINE, 200, "text/html", 13)` | 반환값이 `buf`의 바이트 수와 같고, `buf`가 `"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 13\r\nConnection: close\r\n\r\n"`와 같음 |
| R02 | `http_format_head(buf, MAXLINE, 302, "text/html", 0)` | -1. 표에 없는 상태 코드 |
| R03 | `http_format_head(buf, 10, 200, "text/html", 13)` | -1. 버퍼가 작아 잘림 |
| R04 | `pipe()`로 만든 쓰기 끝에 `http_send_error(fd, 404)` 후 읽기 끝에서 전부 읽음 | 반환 0. 받은 바이트가 R01 형식의 404 헤더(`Content-Length: 49`)와 49바이트 본문 `<html><body><h1>404 Not found</h1></body></html>\n`의 연결과 같음 |

R04는 소켓 대신 파이프를 씁니다. `http_send_error()`는 디스크립터에 쓰기만 하므로 파이프로도 송신 바이트를 그대로 확인할 수 있습니다. 응답은 파이프 버퍼(Linux 기본 64KiB)보다 작아 읽기 전에 써도 막히지 않습니다.

## 기능 테스트

서버를 띄우고 `curl`로 실제 응답을 확인합니다. 준비 절차는 [정적 파일 서빙 테스트의 준비](../static-file-serving/test-cases.md#준비)와 같습니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| R05 | `curl -si http://localhost:15213/missing.html` | 응답 줄 `HTTP/1.1 404 Not found`, `Connection: close`, `Content-Length: 49`. 본문 49바이트 |
| R06 | `curl -si -X DELETE http://localhost:15213/index.html` | `HTTP/1.1 501 Not implemented` |
| R07 | `printf 'GET / HTTP/2.0\r\n\r\n' \| nc localhost 15213` | `HTTP/1.1 505 HTTP version not supported` |

200 응답의 헤더는 [정적 파일 서빙 테스트](../static-file-serving/test-cases.md#정상-응답)에서 함께 확인합니다.
