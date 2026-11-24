# HTTP 요청 파싱 테스트 케이스

[design.md](./design.md)대로 만든 요청 파싱이 맞는지 확인하는 최소 테스트입니다. 기대 결과는 [functions.md](./functions.md)를 따릅니다.

## 단위 테스트

`tests/test_http_request.c`에서 `http_parse_head()`에 문자열을 넘기고 반환값과 구조체를 `assert`로 확인합니다. `http_read_head()`와 `http_read_body()`는 요청 바이트를 `pipe()`에 써 두고 읽기 끝을 `rio_t`로 감싸 시험합니다. 이 방식으로 아래 기능 테스트 Q10, Q11, Q12의 판정(`HTTP_READ_BAD`, `HTTP_READ_CLOSED`)도 소켓 없이 확인하며, 코드 주석의 Q10–Q12는 같은 케이스를 가리킵니다. CTest에 `test_http_request`로 등록하며, 빌드와 등록 방법은 [정적 파일 서빙 설계의 빌드 설계](../static-file-serving/design.md#빌드-설계)에 있습니다.

| ID | 입력 헤더 블록 | 기대 결과 |
| --- | --- | --- |
| Q01 | `GET /index.html HTTP/1.1\r\nHost: ex.com\r\n\r\n` | 0. `method` `GET`, `uri` `/index.html`, `version` `HTTP/1.1`, `host` `ex.com`, `content_length` 0 |
| Q02 | `POST /a HTTP/1.0\r\ncontent-length:  9\r\nCONNECTION: close\r\n\r\n` | 0. `content_length` 9, `connection` `close`. 헤더 이름의 대소문자와 값 앞 공백을 처리함 |
| Q03 | `GET /\r\n\r\n` | 400. 요청 줄 토큰이 두 개 |
| Q04 | `GET index.html HTTP/1.1\r\n\r\n` | 400. URI가 `/`로 시작하지 않음 |
| Q05 | `GET / HTTP/2.0\r\n\r\n` | 505 |
| Q06 | `Content-Length: abc`, `Content-Length: -1`, `Content-Length: 1048577` 헤더를 각각 넣은 요청 | 셋 다 400 |
| Q07 | `GET / HTTP/1.1\r\nNoColonHere\r\n\r\n`, `GET  / HTTP/1.1\r\n\r\n`(공백 두 개), `get / HTTP/1.1\r\n\r\n` | 셋 다 400 |

## 기능 테스트

`http_read_head()`와 `http_read_body()`는 소켓을 읽으므로 서버를 띄우고 `nc`로 확인합니다. 준비 절차는 [정적 파일 서빙 테스트의 준비](../static-file-serving/test-cases.md#준비)와 같습니다. 이 단계의 서버는 GET만 처리하므로 POST의 결과는 501입니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| Q08 | `(printf 'GET /index.html HTTP/1.1\r\nHo'; sleep 1; printf 'st: x\r\n\r\n') \| nc localhost 15213` | 나뉘어 도착한 헤더를 모아 `200 OK` 응답 |
| Q09 | `printf 'POST /a HTTP/1.1\r\nHost: x\r\nContent-Length: 9\r\n\r\nx=15&y=21' \| nc localhost 15213` | `501` 응답. 본문 9바이트를 `\r\n` 없이 읽어 냄 |
| Q10 | `python3 -c "import sys; sys.stdout.write('GET /' + 'a'*9000 + ' HTTP/1.1\r\n\r\n')" \| nc localhost 15213` 후 Q08 실행 | `400` 응답 또는 연결 재설정. 서버가 살아 있어 Q08이 통과 |
| Q11 | `printf 'GET / HTTP/1.1\r\nHost: x\r\n' \| nc -N localhost 15213` 후 Q08 실행 | 첫 연결에는 응답 없음. 서버가 살아 있어 Q08이 통과 |
| Q12 | `printf 'POST /a HTTP/1.1\r\nContent-Length: 9\r\n\r\nx=1' \| nc -N localhost 15213` 후 Q08 실행 | 첫 연결에는 응답 없음. 서버가 살아 있어 Q08이 통과 |

Q10에서 서버는 헤더 블록으로 8191바이트까지만 꺼내고(소켓에서는 `rio_t` 버퍼로 8192바이트를 가져옴) 400을 보낸 뒤 나머지를 읽지 않은 채 연결을 닫습니다. 이때 클라이언트가 응답 대신 연결 재설정을 받을 수 있으므로 두 결과를 모두 통과로 봅니다. 확인할 것은 긴 요청이 서버를 멈추거나 죽이지 않는다는 점입니다.

Q11은 빈 줄 전에, Q12는 본문을 다 받기 전에 클라이언트가 연결을 닫는 경우입니다. `nc -N`은 표준 입력이 끝나면 쓰기 쪽을 닫아 서버에 EOF를 보냅니다. Q08, Q09, Q10의 `nc`는 서버가 연결을 닫을 때 끝납니다.
