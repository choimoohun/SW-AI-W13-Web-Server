# C/POSIX 웹 서버 과제

외부 HTTP 라이브러리 없이 C17과 POSIX 소켓으로 만든 HTTP/1.1 웹 서버입니다. 정적 파일을 제공하고, 프록시 모드에서는 `/api/` 요청을 예약 백엔드로 전달합니다. `/`와 `/index.html`에서 서울 → 광주송정 [기차표 예매 화면](public/index.html)을 확인할 수 있습니다.

## 빠른 실행

Windows에서는 시작 메뉴에서 Docker Desktop을 열고 Linux 엔진이 응답할 때까지 기다립니다. **첫 번째 PowerShell 창**에서 프로젝트 루트로 이동한 뒤 아래 명령으로 확인합니다.

```powershell
docker info --format '{{.OSType}}'
```

`linux`가 출력되면 **같은 창**에서 예매 데모를 시작합니다.

```powershell
docker compose run --build --rm --publish 127.0.0.1:8080:8080 dev sh -ec 'cmake -S . -B /tmp/web-server-build; cmake --build /tmp/web-server-build; /tmp/web-server-build/web_server 8081 public 127.0.0.1 --booking & exec /tmp/web-server-build/web_server 8080 public 0.0.0.0 --proxy 127.0.0.1 8081'
```

`8080` 웹 서버와 `8081` 예약 백엔드의 `Listening` 로그가 둘 다 나오면 첫 창을 열어 둡니다. Chrome에서 [예매 화면](http://127.0.0.1:8080/index.html)을 열어 좌석을 선택하고 **예약하기**를 누르면 예약 상태가 갱신됩니다. 외부에 공개하는 포트는 호스트 루프백의 `8080`이고, 예약 상태는 백엔드를 재시작하면 초기화됩니다.

**두 번째 PowerShell 창**에서 정적 파일과 좌석 조회를 확인합니다.

```powershell
curl.exe -i http://127.0.0.1:8080/index.html
curl.exe -i http://127.0.0.1:8080/api/seats
```

두 응답 모두 `200 OK`가 기대됩니다. 페이지는 HTML을, 좌석 조회는 JSON을 반환합니다. 끝나면 첫 번째 창에서 `Ctrl+C`로 종료합니다. 예약 상태 변경과 프록시 장애 재현은 [동적 처리 직접 확인](docs/dynamic/check.md)을 따릅니다.

## 구현 범위

| 작업 | 현재 동작 |
| --- | --- |
| TCP 연결 | `socket()` → `bind()` → `listen()` → `accept()`로 연결을 받아 요청과 응답을 주고받습니다. |
| HTTP 요청 파싱 | GET·POST의 요청 줄과 헤더를 검사합니다. `Connection` 옵션 목록을 파싱하며 응답 뒤 연결을 닫습니다. 요청 줄과 헤더는 합쳐서 4 KiB까지, `Content-Length` 기반 본문은 8 KiB까지 받습니다. |
| HTTP 응답 생성 | 상태 줄, `Content-Type`·`Content-Length`·`Connection` 헤더, 본문을 전송합니다. API 경로 외의 POST는 받은 본문을 그대로 돌려줍니다. |
| 정적 파일 서빙 | 문서 루트의 HTML·CSS·JS·PNG·JPEG를 MIME 타입과 함께 전송합니다. 파일을 4 KiB씩 읽어 보내고, 누락 파일·접근 제한·서버 오류에 각각 `404`·`403`·`500`으로 응답합니다. |
| 예약 백엔드 | 좌석을 조회하고 예약 상태를 메모리에서 갱신합니다. 같은 좌석을 재예약하면 `409`를 응답합니다. |
| 리버스 프록시 | 경로 접두사 하나를 IPv4 백엔드 한 대에 연결합니다. 본문이 있는 응답은 `Content-Type`·`Content-Length`를 사용하고, 본문 없는 `204`도 지원합니다. 연결 실패나 지원 범위 밖 응답은 `502`, 기한 초과는 `504`입니다. |
| 동시 처리와 종료 | 프록시의 작업 큐는 8칸입니다. 가득 차면 새 연결에 `503`을 시도하고 닫습니다. `SIGINT`·`SIGTERM` 수신 시 연결 수락을 끝내고 워커를 정리합니다. |

예매 화면의 CSS와 JavaScript는 `index.html`에 포함되어 있습니다. 연결당 요청 한 건을 처리하고 `Connection: close`로 응답합니다. 워커 동작, 과부하와 종료 테스트는 [동시 요청 직접 확인](docs/concurrency/check.md)에 있습니다.

## 빌드 및 검증

Docker에서 빌드와 테스트를 실행합니다.

```sh
docker compose run --build --rm dev sh -ec 'cmake -S . -B /tmp/web-server-build && cmake --build /tmp/web-server-build && ctest --test-dir /tmp/web-server-build --output-on-failure'
```

CTest는 분할 POST, 정적 파일, 예약·프록시·동시 처리의 세 시나리오를 검증합니다. macOS, Linux 또는 WSL2 Ubuntu에서 직접 빌드하려면 CMake 3.16 이상과 C17 컴파일러를 사용합니다. 테스트가 켜진 빌드에는 Perl이 필요하며, 서버 바이너리만 빌드하려면 `-DBUILD_TESTING=OFF`를 지정할 수 있습니다.

```sh
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

로컬 POSIX 환경에서 예매 화면을 확인할 때는 두 터미널에서 각각 실행합니다.

```sh
./build/web_server 8081 public 127.0.0.1 --booking
./build/web_server 8080 public 127.0.0.1 --proxy 127.0.0.1 8081
```

프록시는 기본값 `/api/`로 전달할 요청을 고릅니다. 다른 접두사를 쓰려면 백엔드 포트 뒤에 슬래시로 끝나는 값을 추가합니다. 백엔드는 원래 요청 경로를 받으므로 그 경로를 처리할 수 있어야 합니다. `web_server [port [document-root [bind-address]]]`는 단일 연결 HTTP 실습용이며 기본값은 `8080 public 127.0.0.1`입니다.

## 프로젝트 문서

- [과제 명세](docs/spec.md) · 요구사항, 구현 범위와 완료 기준
- [서버 설계](docs/design.md) · 현재 요청 처리 흐름과 좌석 예약·프록시·동시 처리 설계도
- [진행 상태](docs/progress.md) · 마지막 검증과 다음 작업
- [검증 결과](docs/results.md) · 통합 테스트, 부하 측정, 메모리·FD 검사
