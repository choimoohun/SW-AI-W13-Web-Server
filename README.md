# C/POSIX 웹 서버 과제

외부 라이브러리 없이 표준 C와 POSIX API로 웹 서버를 만드는 과제입니다. CS:APP 3판 11~12장의 예제에서 출발해 단계별로 확장합니다. 단계 순서와 진행 상태는 [docs/README.md](docs/README.md)에 있습니다.

## Docker Desktop — Windows / macOS

Windows에서는 Docker Desktop을 **WSL 2 백엔드 및 Linux 컨테이너 모드**로 실행합니다. macOS에서는 칩에 맞는 Docker Desktop을 실행합니다. 아래 명령은 PowerShell과 macOS 터미널 모두에서 프로젝트 루트에서 실행합니다.

```sh
docker compose run --build --rm dev
```

개발 이미지를 빌드하고, 호스트 소스를 `/workspace`에 마운트한 뒤 CMake 구성과 빌드를 실행합니다. 빌드 결과는 컨테이너 안 `/tmp/web-server-build`에만 생성되며 컨테이너 종료 시 제거됩니다.

같은 개발 환경에서 직접 명령을 실행하려면:

```sh
docker compose run --rm dev sh
```

`httpd`는 모든 주소(`0.0.0.0`)에서 듣습니다. 호스트 브라우저에서 접속하려면 셸을 띄울 때 호스트의 루프백 주소에만 포트를 매핑합니다: `docker compose run --rm -p 127.0.0.1:8000:8000 dev sh`.

## 네이티브 빌드 — macOS / Linux / WSL2 Ubuntu

CMake 3.16 이상과 C17 컴파일러를 설치합니다. macOS에서는 Xcode Command Line Tools의 Clang을 사용할 수 있습니다. Windows 네이티브 빌드는 지원하지 않습니다.

```sh
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

CTest에 단위 테스트 6개(`test_http_request`, `test_http_response`, `test_static_file`, `test_cgi`, `test_conn_queue`, `test_proxy`)가 등록되어 있습니다. Docker 이미지에는 기능·부하·메모리 검사에 쓰는 `curl`, `wrk`, `valgrind`가 들어 있습니다.

## 실행

```sh
./build/httpd <port> <root> [--route <prefix>=<host:port>[,<host:port>...]]...

# 예: /api는 백엔드 두 대에 번갈아 보내고, 나머지는 /srv/www의 정적 파일과 CGI로 처리
./build/httpd 8000 /srv/www --route /api=127.0.0.1:8001,127.0.0.1:8002
```

`--route`는 16개까지, 라우트마다 백엔드는 16대까지 쓸 수 있습니다. `--route`를 주지 않으면 정적 파일과 CGI만 처리합니다. 워커 수(8)는 `src/httpd.c`의 `NWORKERS` 상수라 실행 옵션으로 바꿀 수 없습니다. 인자가 잘못되면 원인을 출력하고 종료 코드 1로 끝납니다. `SIGINT`·`SIGTERM`을 받으면 이미 받은 요청에 모두 응답한 뒤 종료합니다.

## 구현된 기능

외부 HTTP·이벤트 루프 라이브러리 없이 표준 C17과 POSIX API만 씁니다.

| 단계 | 기능 |
| --- | --- |
| 1. [TCP 소켓 서버](docs/tcp-socket-server/design.md) | `socket` → `bind` → `listen` → `accept`. 끊긴 소켓에 써도 죽지 않도록 `SIGPIPE` 무시 |
| 2. [HTTP 요청 파싱](docs/http-request-parsing/design.md) | 요청 줄, `Host`·`Connection`·`Content-Length` 헤더, `Content-Length` 기준 본문(최대 1 MiB). `Transfer-Encoding`은 501, `HTTP/1.0`·`HTTP/1.1`이 아닌 `HTTP/` 버전은 505(그 밖의 형식은 400) |
| 3. [HTTP 응답 생성](docs/http-response/design.md) | 상태 줄, `Content-Type`, `Content-Length`, `Connection: close`, HTML 오류 본문. 200, 400, 403, 404, 500, 501, 502, 504, 505 |
| 4. [정적 파일 서빙](docs/static-file-serving/design.md) | `GET`. 확장자별 MIME 타입, `/`로 끝나면 `index.html`, `..`·`.`·`//` 경로는 403. 연결마다 읽기·쓰기 5초 제한 |
| 5. [동적 요청 처리](docs/dynamic-request/design.md) | `/cgi-bin/` 아래 프로그램을 `fork`·`exec`로 실행(`GET`, `POST`). 출력을 파이프로 다 받은 뒤 상태 코드를 정함 |
| 6. [동시 요청 처리](docs/concurrency/design.md) | 워커 스레드 8개와 연결 대기열 16칸. 신호를 받으면 새 연결을 멈추고 남은 요청을 처리한 뒤 종료 |
| 7. [리버스 프록시](docs/reverse-proxy/design.md) | `--route` 경로 접두사 라우팅(가장 긴 접두사 우선), 라운드로빈 로드 밸런싱, hop-by-hop 헤더 제거. 백엔드 연결 실패·비정상 응답은 502, 5초 시간 초과는 504 |

요청은 `--route` 접두사 → `/cgi-bin/` → 정적 파일 순서로 처리합니다. 단계별 설계, 함수 명세, 테스트 절차는 [docs/README.md](docs/README.md)에서 시작합니다.

## 알려진 한계

- keep-alive가 없습니다. 클라이언트·백엔드 양쪽 모두 요청 하나마다 연결을 닫습니다.
- 워커가 8개뿐이라, 응답하지 않는 백엔드에 요청이 몰리면 정적 파일과 CGI 요청도 최대 5초씩 기다립니다.
- 청크 인코딩을 읽지 못합니다. 청크 요청은 501, 청크로 응답하는 백엔드는 502입니다.
- 파일 디스크립터 한도에 닿으면 서버가 종료됩니다(후속 과제).
- Forward Proxy, 캐싱, 설정 파일, `Host` 헤더 기반 라우팅은 없습니다.
