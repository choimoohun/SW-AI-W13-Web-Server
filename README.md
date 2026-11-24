# C/POSIX 웹 서버 과제

외부 HTTP 라이브러리 없이 C17과 POSIX 소켓으로 만든 HTTP/1.1 웹 서버입니다. TCP 연결을 받아 HTTP 요청을 읽고, GET 요청에 문서 루트의 파일을 응답합니다. `/`와 `/index.html`에서 서울 → 광주송정 [기차표 예매 화면](public/index.html)을 확인할 수 있습니다.

## 빠른 실행

Windows에서는 Docker Desktop을 시작 메뉴에서 열고 Linux 컨테이너 모드로 실행합니다. 프로젝트 루트의 터미널에서 다음 명령을 실행합니다.

```sh
docker compose run --build --rm --publish 127.0.0.1:8080:8080 dev sh -ec 'cmake -S . -B /tmp/web-server-build && cmake --build /tmp/web-server-build && exec /tmp/web-server-build/web_server 8080 public 0.0.0.0'
```

`Listening on 0.0.0.0:8080 (root: public)`이 나오면 Chrome에서 [예매 화면](http://127.0.0.1:8080/index.html)을 엽니다. 좌석을 클릭하거나 `Tab`과 `Enter`로 선택하면 선택 내역이 바뀝니다.

서버는 **연결 한 건에서 요청 하나에 응답한 뒤 종료**합니다. 브라우저를 새로고침하거나 아래 예시를 실행할 때마다 서버를 다시 시작합니다. PowerShell에서는 별도 터미널에서 응답을 확인할 수 있습니다.

```powershell
curl.exe -i http://127.0.0.1:8080/index.html
```

정상 응답은 `HTTP/1.1 200 OK`와 `Content-Type: text/html; charset=utf-8`을 포함하며, 본문에는 HTML 파일이 들어갑니다. `/missing.html`로 요청하면 `404 Not Found`를 반환합니다.

## 구현 범위

| 작업 | 현재 동작 |
| --- | --- |
| TCP 연결 | `socket()` → `bind()` → `listen()` → `accept()`로 연결을 받아 요청과 응답을 주고받습니다. |
| HTTP 요청 파싱 | GET·POST의 요청 줄과 헤더를 검사합니다. 요청 줄과 헤더는 합쳐서 4 KiB까지, `Content-Length` 기반 본문은 8 KiB까지 받습니다. |
| HTTP 응답 생성 | 상태 줄, `Content-Type`·`Content-Length`·`Connection` 헤더, 본문을 전송합니다. POST는 받은 본문을 그대로 돌려주는 실습 동작입니다. |
| 정적 파일 서빙 | 문서 루트의 HTML·CSS·JS·PNG·JPEG를 MIME 타입과 함께 전송합니다. 파일을 4 KiB씩 읽어 보내고, 누락 파일·접근 제한·서버 오류에 각각 `404`·`403`·`500`으로 응답합니다. |

예매 화면의 CSS와 JavaScript는 `index.html` 한 파일에 포함되어 있습니다. 좌석 선택은 브라우저에서 동작하며, 동적 예약·프록시·동시 요청 처리와 부하 테스트는 후속 단계입니다. 현재 HTTP 응답은 `Connection: close`를 사용합니다.

## 빌드 및 검증

Docker에서 빌드와 테스트를 실행합니다.

```sh
docker compose run --build --rm dev sh -ec 'cmake -S . -B /tmp/web-server-build && cmake --build /tmp/web-server-build && ctest --test-dir /tmp/web-server-build --output-on-failure'
```

CTest는 분할 POST 본문과 정적 파일의 바이트·MIME 타입·오류 응답을 검증합니다. macOS, Linux 또는 WSL2 Ubuntu에서 직접 빌드하려면 CMake 3.16 이상과 C17 컴파일러를 사용합니다. 통합 테스트를 실행하려면 Perl이 필요합니다.

```sh
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
./build/web_server
```

실행 형식은 `web_server [port [document-root [bind-address]]]`입니다. 기본값은 `8080 public 127.0.0.1`입니다. 전체 과제 요구사항은 [SPEC.md](docs/SPEC.md), 마지막 검증과 다음 작업은 [PROGRESS.md](docs/PROGRESS.md)를 참고합니다.
