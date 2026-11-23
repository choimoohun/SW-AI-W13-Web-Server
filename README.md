# C/POSIX 웹 서버 과제

현재는 **개발 환경만** 구성되어 있습니다. C 소스, 서버 실행 파일, 테스트 케이스가 없으므로 아직 HTTP 포트를 열거나 요청을 처리하지 않습니다.

## Docker Desktop — Windows / macOS

Windows에서는 Docker Desktop을 **WSL 2 백엔드 및 Linux 컨테이너 모드**로 실행합니다. macOS에서는 칩에 맞는 Docker Desktop을 실행합니다. 아래 명령은 PowerShell과 macOS 터미널 모두에서 프로젝트 루트에서 실행합니다.

```sh
docker compose run --build --rm dev
```

개발 이미지를 빌드하고, 호스트 소스를 `/workspace`에 마운트한 뒤 CMake 구성과 빌드를 실행합니다. 지금은 C 컴파일러와 POSIX `socket()` 사용 가능 여부를 확인하며 실행 파일은 생성하지 않습니다. 빌드 결과는 컨테이너 안 `/tmp/web-server-build`에만 생성되며 컨테이너 종료 시 제거됩니다.

같은 개발 환경에서 직접 명령을 실행하려면:

```sh
docker compose run --rm dev sh
```

서버 구현 후 호스트 브라우저에서 접속하려면 서버를 컨테이너의 `0.0.0.0`에 바인딩하고, Compose에서 호스트의 루프백 주소에 포트를 매핑합니다. 현재는 공개할 포트가 없어 포트 매핑을 넣지 않았습니다.

## 네이티브 빌드 — macOS / Linux / WSL2 Ubuntu

CMake 3.16 이상과 C17 컴파일러를 설치합니다. macOS에서는 Xcode Command Line Tools의 Clang을 사용할 수 있습니다. Windows 네이티브 빌드는 지원하지 않습니다.

```sh
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

CTest에는 HTTP 요청 파싱 단위 테스트 `test_http_request`가 등록되어 있습니다. Docker 이미지에는 기능·부하·메모리 검사에 사용할 `curl`, `wrk`, `valgrind`가 포함되어 있습니다.

## 설계 문서

- TCP 소켓 서버(CS:APP 11.4 에코 서버): [설계](docs/tcp-socket-server/design.md), [함수 명세](docs/tcp-socket-server/functions.md), [테스트 케이스](docs/tcp-socket-server/test-cases.md)
- HTTP 요청 파싱(CS:APP 11.5.3): [설계](docs/http-request-parsing/design.md), [함수 명세](docs/http-request-parsing/functions.md), [테스트 케이스](docs/http-request-parsing/test-cases.md)
- HTTP 응답 생성(CS:APP 11.5.3): [설계](docs/http-response/design.md), [함수 명세](docs/http-response/functions.md), [테스트 케이스](docs/http-response/test-cases.md)
- 정적 파일 서빙(CS:APP 11.5.2): [설계](docs/static-file-serving/design.md), [함수 명세](docs/static-file-serving/functions.md), [테스트 케이스](docs/static-file-serving/test-cases.md)

## 구현 단계에서 지킬 범위

- 표준 C와 POSIX API로 TCP 연결 수락 및 송수신, HTTP 요청 파싱(`GET`, `POST`, 헤더, 본문), 응답 생성을 직접 구현합니다. 외부 HTTP·이벤트 루프 라이브러리는 사용하지 않습니다.
- 정적 파일 서빙(MIME 타입, `403`·`404`·`500`), 동적 요청 처리, 동시 연결 처리를 구현합니다.
- Forward Proxy와 Reverse Proxy 중 최소 하나를 구현합니다. 포트와 정적 파일 루트, Reverse Proxy 선택 시 라우팅 규칙을 CLI 인자나 설정 파일로 받습니다.
- 구현 후 단위·HTTP 기능·동시 접속·부하·메모리 누수 검사를 추가합니다. 불완전한 요청, 큰 본문·긴 URI, 연결 종료, 백엔드 타임아웃(Reverse Proxy 선택 시)을 포함합니다.
