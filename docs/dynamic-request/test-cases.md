# 동적 요청 처리 테스트 케이스

[design.md](./design.md)대로 만든 CGI 실행이 맞는지 확인하는 최소 테스트입니다.

## 준비

[정적 파일 서빙 테스트의 준비](../static-file-serving/test-cases.md#준비)에 이어 CGI 스크립트를 만듭니다.

```sh
mkdir -p /tmp/www/cgi-bin
cat > /tmp/www/cgi-bin/adder <<'EOF'
#!/bin/sh
a=${QUERY_STRING%%&*}; b=${QUERY_STRING#*&}
printf 'Content-Type: text/html\r\n\r\n'
echo "sum=$((a + b))"
EOF
cat > /tmp/www/cgi-bin/post <<'EOF'
#!/bin/sh
printf 'Content-Type: text/plain\r\n\r\n'
head -c "$CONTENT_LENGTH"
EOF
chmod 755 /tmp/www/cgi-bin/*
```

## 단위 테스트

`tests/test_cgi.c`에 작성하고 CTest에 `test_cgi`로 등록합니다. D04~D09와 D15는 임시 디렉터리에 셸 스크립트를 만들고 `socketpair()`의 한쪽 끝으로 `cgi_serve()`를 부른 뒤 다른 끝에서 응답을 읽습니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| D01 | `cgi_parse_output()`에 `"Content-Type: text/plain\r\n\r\nhi"` | 0, 타입 `text/plain`, 본문 `hi` |
| D02 | `"X-A: 1\ncontent-type:text/html\n\n<b>"` | 0, 타입 `text/html`, 본문 `<b>`. 대소문자 무시, LF 줄 끝 |
| D03 | `Content-Type` 없음, 빈 줄 없음, 값이 공백뿐, 값 중간에 `\r`, 값이 256바이트, 빈 출력 | 모두 -1. 255바이트 값은 0 |
| D04 | 환경 변수와 stdin을 출력하는 스크립트를 POST, `/cgi-bin/echo?x=1&y=2`, 본문 `abc`로 | `200 OK`, `Content-Type: text/plain`, `Content-Length: 18`, 본문 `POST\|x=1&y=2\|3\|abc` |
| D05 | 같은 스크립트를 GET, 쿼리 없이 | 본문 `GET\|\|0\|` |
| D06 | 출력 후 `exit 1`하는 스크립트 | `500` |
| D07 | 없는 파일, 권한 `0644`인 스크립트 | `404`, `403` |
| D08 | `sleep 30`만 하는 스크립트 | `500`, 8초 안에 반환. 손자 `sleep`까지 죽어 응답 소켓에 EOF가 옴 |
| D09 | 헤더와 본문을 쓰고 `exec >&-`로 표준 출력을 닫은 뒤 `sleep 30` | `500`, 8초 안에 반환 |
| D15 | 헤더 뒤에 `head -c 2000000 /dev/zero`로 1MiB를 넘게 쓰는 스크립트 | `500`. 출력 상한에서 프로세스 그룹을 죽이고 회수 |

## 기능 테스트

터미널 A에서 `./build/httpd 15213 /tmp/www`로 서버를 실행합니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| D10 | `curl -s 'http://localhost:15213/cgi-bin/adder?3&4'` | `sum=7` |
| D11 | `curl -s --data 'hello body' http://localhost:15213/cgi-bin/post` | `hello body` |
| D12 | `curl -s -o /dev/null -w '%{http_code}\n' --data x http://localhost:15213/index.html` | `501`. POST는 CGI에만 허용 |
| D13 | `curl -s -o /dev/null -w '%{http_code}\n' http://localhost:15213/cgi-bin/nope` | `404` |
| D14 | Debug 빌드에 `-fsanitize=address,undefined`를 켜고 D10~D13을 보낸 뒤 서버에 `SIGTERM` | sanitizer 보고 없음 |
| D16 | `cp /tmp/www/cgi-bin/adder /tmp/www/cgi-bin/index.html` 후 `curl -s -o /dev/null -w '%{http_code}\n' http://localhost:15213/cgi-bin/` | `403`. 디렉터리 요청에 `index.html`을 붙여 실행하지 않음 |

경로 정규화는 [정적 파일 서빙 S17](../static-file-serving/test-cases.md#단위-테스트)이 확인합니다. 기능 테스트로는 `curl -s --path-as-is -o /dev/null -w '%{http_code}\n' http://localhost:15213//cgi-bin/adder`가 `403`이어야 합니다.
