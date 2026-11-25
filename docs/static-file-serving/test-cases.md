# 정적 파일 서빙 테스트 케이스

[design.md](./design.md)대로 만든 정적 파일 서빙이 맞는지 확인하는 최소 테스트입니다. 기대 결과는 [functions.md](./functions.md)를 따릅니다. [요청 파싱](../http-request-parsing/test-cases.md)과 [응답 생성](../http-response/test-cases.md)의 기능 테스트도 이 문서의 준비 절차를 씁니다.

## 준비

Docker에서는 [TCP 소켓 서버 테스트의 준비](../tcp-socket-server/test-cases.md#준비)처럼 같은 컨테이너에 터미널 두 개를 붙이고, `nc`와 `python3`를 설치한 뒤 빌드합니다. 단위 테스트는 `(cd build && ctest --output-on-failure)`로 실행합니다.

기능 테스트용 루트를 만듭니다.

```sh
mkdir -p /tmp/www/dir
printf '<b>Hello</b>\n' > /tmp/www/index.html
printf 'body{}\n' > /tmp/www/style.css
head -c 3000 /dev/urandom > /tmp/www/cat.png
: > /tmp/www/empty.txt
printf 'secret\n' > /tmp/www/secret.html && chmod 000 /tmp/www/secret.html
```

터미널 A에서 `./build/httpd 15213 /tmp/www`로 서버를 실행합니다.

## 단위 테스트

`tests/test_static_file.c`에 작성하고 CTest에 `test_static_file`로 등록합니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| S01 | `mime_type()`에 `a.html`, `A.PNG`, `x.jpeg`, `a.jpg`, `x.js`, `noext`, `v1.0/file` | `text/html`, `image/png`, `image/jpeg`, `image/jpeg`, `text/javascript`, `application/octet-stream`, `application/octet-stream` |
| S02 | 루트 `/srv/www`로 `static_resolve_path()`에 `/`, `/style.css?v=2`, `/a..b.html` | 셋 다 0. `/srv/www/index.html`, `/srv/www/style.css`, `/srv/www/a..b.html` |
| S03 | 루트 `/srv/www`로 `/../etc/passwd`, `/a/..`, `/a/../b` | 셋 다 403 |
| S04 | `size`를 16으로 두고 `/style.css` | 400. 결과 `/srv/www/style.css`가 버퍼보다 김 |
| S15 | 임시 디렉터리에 `mkfifo()`로 만든 FIFO를 `socketpair()`의 한쪽 끝으로 `static_serve()` | 막히지 않고 `HTTP/1.1 403`으로 시작하는 응답. 5초 안에 끝나지 않으면 `alarm()`으로 실패 |
| S17 | 루트 `/srv/www`로 `//cgi-bin/x`, `/./cgi-bin/x`, `/cgi-bin//x`, 그리고 `/dir/`, `/.hidden` | 앞의 셋은 403, 뒤의 둘은 0. [CGI 소스 노출](../dynamic-request/design.md#접두사-검사와-경로-정규화) 방지 |
| S18 | `static_has_bad_segments()`에 `/api/a..b`, `/api?x=/../`, `/api/../x`, `/api/./x`, `//api` (7단계) | 앞의 둘은 0, 뒤의 셋은 1. `?` 뒤는 검사하지 않음. [리버스 프록시](../reverse-proxy/design.md)가 라우팅 전에 같은 검사를 씀 |

## 정상 응답

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| S05 | `curl -si http://localhost:15213/index.html` | `HTTP/1.1 200 OK`, `Content-Type: text/html`, `Content-Length: 13`, `Connection: close`. 본문 `<b>Hello</b>` |
| S06 | `curl -s http://localhost:15213/ \| cmp - /tmp/www/index.html` | 차이 없음. `/`가 `index.html`로 확장됨 |
| S07 | `curl -s http://localhost:15213/cat.png \| cmp - /tmp/www/cat.png` | 차이 없음. `0x00`이 들어 있는 이진 파일도 바이트 그대로 전달 |
| S08 | `curl -si 'http://localhost:15213/style.css?v=2'` | `200`, `Content-Type: text/css` |
| S09 | `curl -si http://localhost:15213/empty.txt` | `200`, `Content-Length: 0`, 본문 없음 |

## 오류 응답

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| S10 | `curl -si --path-as-is http://localhost:15213/../etc/passwd` | `403 Forbidden` |
| S11 | `curl -si http://localhost:15213/dir` | `403 Forbidden`. 일반 파일이 아님 |
| S12 | `curl -si http://localhost:15213/dir/` | `404 Not found`. `index.html` 없음 |
| S13 | 서버를 `su -s /bin/sh nobody -c './build/httpd 15213 /tmp/www'`로 다시 실행한 뒤 `curl -si http://localhost:15213/secret.html` | `403 Forbidden` |

- S10에 `--path-as-is`가 필요한 이유는 `curl`이 기본으로 `/../`를 지운 경로를 보내기 때문입니다.
- S13은 `root`로 실행하면 권한 비트를 무시하고 파일이 열려 `200`이 되므로 일반 사용자로 실행합니다. `nobody`가 `./build/httpd`를 실행하지 못하면 `httpd`를 `/tmp`에 복사해 실행합니다.
- 파일 없음(404)은 [응답 생성 테스트 R06](../http-response/test-cases.md#기능-테스트)가 확인합니다.
- 500(본문 할당 실패, `open()`과 `fstat()`의 예외적 실패)은 테스트로 재현하기 어려우므로 코드 리뷰로 확인합니다. 500 응답의 바이트 형식은 R04와 같은 `http_send_error()` 경로라 따로 확인하지 않습니다.

## 시간 제한

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| S16 | 터미널 B에서 `(printf 'POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nhello'; sleep 30) \| nc localhost 15213`을 띄운 채, 터미널 C에서 `time curl -s -o /dev/null -w '%{http_code}\n' http://localhost:15213/` | 터미널 C가 6초 안에 `200`. 터미널 B는 약 5초 뒤 응답 없이 서버가 연결을 닫음 |

## 메모리

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| S14 | Debug 빌드 후 `valgrind --leak-check=full ./build-debug/httpd 15213 /tmp/www`로 실행하고 S05, S07, S10, [Q09](../http-request-parsing/test-cases.md#기능-테스트)를 보낸 뒤 `Ctrl+C` | 잘못된 읽기와 쓰기 없음. `definitely lost: 0 bytes`. Q09의 POST 본문 할당이 해제됨 |
