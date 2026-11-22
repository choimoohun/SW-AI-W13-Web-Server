# REQUIREMENTS.md — C POSIX 웹서버 및 프록시 서버

## 1. 프로젝트 개요 및 목표
- **목적**: 외부 HTTP/네트워크 프레임워크 없이 C 언어와 POSIX 소켓 API만을 사용하여 HTTP/1.1 웹서버 및 프록시(Reverse Proxy / Forward Proxy)를 구현한다.
- **핵심 가치**:
  1. TCP/IP 소켓부터 HTTP 파싱, 동시성 처리, 프록시 라우팅까지의 동작 원리를 코드 레벨에서 설명할 수 있어야 한다.
  2. 메모리 누수, 버퍼 오버플로우, 비정상 종료가 없는 견고한 시스템 프로그램을 작성한다.
  3. 자동화된 단위·기능·부하·메모리 테스트 결과를 데모(README.md 기준)로 증명한다.

## 1.1 각 요구사항이 푸는 문제

아래 표는 요구사항을 읽을 때 돕기 위한 **해석**입니다. 과제 원문 자체가 아니며,
원문은 변경하지 않습니다. 출발점에 대한 역사적 주장이 아니라, 이 프로젝트가
다루는 문제를 요구사항별로 정리한 것입니다.

| 요구사항 | 푸는 문제 |
|---|---|
| FR-01 TCP 소켓 서버 | 연결을 받을 entry point가 없고, 재시작 시 포트를 다시 써야 함 |
| FR-02 HTTP/1.1 파싱 | TCP는 바이트 스트림만 주므로 요청의 시작과 끝을 정해야 함 |
| FR-03 HTTP 응답 | 클라이언트가 해석할 상태·헤더·본문 구조가 없음 |
| FR-04 정적 파일 서빙 | 파일을 그대로 주면 경로 탈출, MIME 누락, 바이너리 손상 위험이 있음 |
| FR-05 동적 요청 처리 | 정적 파일만으로는 요청에 따라 달라지는 결과를 만들 수 없음 |
| FR-06 동시 요청 처리 | 느린 클라이언트 하나가 다른 요청을 막고, 100개 이상을 동시에 받아야 함 |
| FR-07 Reverse Proxy | 한 프로세스에 서비스를 묶으면 배포·장애·확장이 같이 묶임 |
| FR-08 Forward Proxy | 클라이언트를 대신해 요청하는 경우가 필요하지만 이번 범위 밖 |
| NFR-01 메모리 안전 | 요청 처리 중 새는 메모리와 잘못된 메모리 접근이 쌓임 |
| NFR-02 시그널/Graceful Shutdown | 프로세스가 바로 죽으면 진행 중 자원과 연결을 정리하지 못함 |
| NFR-03 타임아웃/연결 수명 | 유휴 연결과 느린 백엔드가 자원을 무한히 점유함 |
| NFR-04 로깅/관측 | 요청이 처리돼도 어떤 워커가 얼마나 걸렸는지 남지 않음 |

자세한 문제 배경은 [why_webserver.md](./why_webserver.md)에 있습니다.

---

## 2. 기술 스택 및 환경 제약 (Constraints)
- **C-01 (언어)**: C11 표준 (`gcc` 또는 `clang`, 컴파일 플래그 `-Wall -Wextra -Werror` 권장).
- **C-02 (라이브러리 제한)**:
  - **허용**: 표준 C 라이브러리, POSIX 시스템 API (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>`, `<pthread.h>`, `<poll.h>`, `<unistd.h>`, `<signal.h>`, `<sys/stat.h>` 등).
  - **금지**: `libevent`, `libuv`, `mongoose`, `llhttp`, `curl` 등 외부 HTTP/이벤트 루프/네트워크 라이브러리.
- **C-03 (OS 이식성)**: Linux(Ubuntu / WSL2) 및 macOS 환경 모두에서 동일 소스로 빌드 및 실행 가능해야 한다. (`epoll`/`kqueue` 등 OS 종속 API 사용 시 `#ifdef`로 분기하거나 POSIX 표준 `poll()` 사용).
- **C-04 (빌드 시스템)**: 루트 디렉토리에 `Makefile`을 제공하며 최소 `make`, `make test`, `make asan`, `make clean` 타겟을 지원한다.
- **C-05 (실행 인터페이스)**:
  - CLI 인자 또는 설정 파일로 포트, 정적 파일 루트, 프록시 라우팅 규칙을 주입받는다.
  - 예: `./webserver -p <port> -r <doc_root> -c <config_file>`
  - 결정: `key = value` 형태의 단순 `.conf` 포맷(`conf/tcpserver.conf`, `conf/backends.conf`).

---

## 3. 스코프 및 우선순위 (MoSCoW)
- **Must (필수 - MVP)**: TCP 서버, HTTP/1.1 파싱(GET/POST), 정적 파일 서빙, 동시성 처리, Reverse Proxy(경로 기반 라우팅), 시그널/에러 처리, 단위/기능/누수 테스트.
- **Should (권장)**: `Connection: keep-alive` 지원, Reverse Proxy Round-Robin 로드 밸런싱, 내장 동적 엔드포인트와 백엔드 프록시, 부하 테스트 리포트.
- **Could (선택 - 여유 시)**: Forward Proxy 구현, Forward Proxy 인메모리 캐싱, Host 헤더 기반 가상 호스팅 라우팅.

---

## 4. 기능 요구사항 (Functional Requirements)

### FR-01. TCP 소켓 서버
- **명세**: `socket() → setsockopt(SO_REUSEADDR) → bind() → listen() → accept()` 표준 흐름으로 연결을 수락한다.
- **수용 기준 (AC)**:
  - 서버 재시작 시 `Address already in use` 없이 동일 포트에 즉시 바인딩된다.
  - `listen` backlog는 최소 128 이상으로 설정한다.

### FR-02. HTTP/1.1 요청 파싱
- **명세**:
  - 요청 라인 파싱: `Method SP Request-URI SP HTTP-Version CRLF`
  - 최소 지원 메서드: `GET`, `POST` (그 외 메서드는 `405 Method Not Allowed` 또는 `501 Not Implemented` 응답).
  - 지원 버전: `HTTP/1.0`, `HTTP/1.1` (그 외 잘못된 형식은 `400 Bad Request`, 미지원 버전은 `505 HTTP Version Not Supported`).
  - 주요 헤더 파싱(대소문자 무관): `Host`, `Content-Length`, `Connection`.
  - 본문(Body) 처리: `POST` 요청 시 `Content-Length` 바이트만큼 정확히 읽어 핸들러에 전달한다.
- **제한 수치 (방어용 상한)**:
  - 최대 URI 길이: `2048` bytes (초과 시 `414 URI Too Long`)
  - 최대 헤더 전체 크기: `8192` bytes (초과 시 `431 Request Header Fields Too Large` 또는 `400`)
  - 최대 요청 본문 크기: `1048576` bytes (1MB, 초과 시 `413 Payload Too Large`)
  - 결정: `Transfer-Encoding: chunked` 미지원, `501 Not Implemented` 반환.
- **수용 기준 (AC)**:
  - TCP 패킷이 여러 조각으로 나뉘어(short read) 도착해도 `\r\n\r\n` 및 `Content-Length` 경계를 정확히 조립한다.
  - `HTTP/1.1` 요청에 `Host` 헤더가 누락된 경우 `400 Bad Request`를 반환한다.

### FR-03. HTTP 응답 생성
- **명세**:
  - 상태 라인(`HTTP/1.1 <code> <reason>\r\n`), 표준 헤더(`Content-Type`, `Content-Length`, `Connection`, `Date` 또는 `Server`), 빈 줄(`\r\n`), 본문을 전송한다.
- **수용 기준 (AC)**:
  - 부분 쓰기(short write)가 발생해도 전체 바이트가 전송될 때까지 루프 처리한다.
  - `curl -i` 및 웹 브라우저가 응답을 경고 없이 정상 파싱한다.

### FR-04. 정적 파일 서빙
- **명세**:
  - 지정된 루트 디렉토리(`doc_root`) 하위 파일을 읽어 응답한다.
  - `/` 요청 시 기본으로 `index.html`을 서빙한다.
  - MIME 타입 매핑: `.html` (`text/html`), `.css` (`text/css`), `.js` (`application/javascript`), `.json` (`application/json`), `.png` (`image/png`), `.jpg`/`.jpeg` (`image/jpeg`), `.txt` (`text/plain`), 기타 (`application/octet-stream`).
  - 보안: `..` 경로 탈출(Path Traversal) 시도 시 `doc_root` 밖의 접근을 차단하고 `403 Forbidden`을 반환한다.
- **수용 기준 (AC)**:
  - 존재하는 파일은 `200 OK`, 권한 없음/경로 탈출 시도는 `403`, 없는 파일은 `404`, 서버 내부 I/O 오류는 `500`을 반환한다.
  - 바이너리 파일(PNG/JPG)이 손상 없이 원본 바이트와 일치하게 전송된다.

### FR-05. 동적 요청 처리
- **결정**: 안 B(내장 동적 핸들러 + 백엔드 프록시) 채택. 안 A(CGI)는 구현하지 않음.
- **명세**: 아래 방식 중 1개 이상 선택
  - **안 A (CGI)**: 특정 경로(예: `/cgi-bin/*`) 요청 시 `fork()` + `pipe()` + `execve()`로 외부 스크립트/실행 파일을 실행하고 표준 출력(`stdout`)을 HTTP 응답으로 반환한다. (`POST` 본문은 `stdin`으로 전달)
  - **안 B (내장 동적 핸들러 + 백엔드 프록시)**: `/health`, `/echo`(POST JSON/폼 에코) 등의 내장 동적 엔드포인트 처리 및 `/api/*` 백엔드 서버 전달.
- **수용 기준 (AC)**:
  - `POST` 요청으로 전달한 데이터가 동적 처리 결과에 반영되어 클라이언트로 반환된다.
  - CGI 자식 프로세스 종료 시 좀비 프로세스가 남지 않는다(`waitpid`).

### FR-06. 동시 요청 처리
- **결정**: 안 A(고정 워커 풀) + 안 B(`iomux` 이벤트 루프) 결합. 안 C(`fork`)는 사용하지 않음.
- **명세**: 아래 중 1개 확정
  - **안 A (권장 기본안)**: `pthread` 기반 고정 크기 스레드 풀 (Worker Thread Pool + Bounded Task Queue).
  - **안 B**: `poll()`(또는 `epoll`/`kqueue`) 기반 논블로킹 이벤트 루프.
  - **안 C**: `fork()` 기반 멀티프로세스.
- **수용 기준 (AC)**:
  - 한 클라이언트가 연결을 열고 데이터를 늦게 보내는(Slow client) 동안에도 다른 클라이언트의 요청이 즉시 처리된다.
  - 동시 접속 100개 이상의 요청을 유실 없이 처리한다.

### FR-07. Reverse Proxy (필수)
- **명세**:
  - 설정된 라우팅 규칙에 따라 요청을 내부 백엔드 서버로 중계한다.
    - 예: `/api/*` → 백엔드 풀(`127.0.0.1:8001`, `127.0.0.1:8002`)
    - 결정: Path Prefix 기반. `route = prefix|backend...|rewrite`의 rewrite로 `/api/users` → `/users`처럼 경로를 바꿀 수 있음.
  - **로드 밸런싱**: 동일 경로에 여러 백엔드가 등록된 경우 Round-Robin 방식으로 분산한다.
  - **헤더 보강**: 업스트림 전달 시 `X-Forwarded-For` 헤더를 추가/갱신한다.
  - **오류 처리**:
    - 백엔드 연결 실패 / 비정상 응답 → `502 Bad Gateway`
    - 백엔드 응답 타임아웃 → `504 Gateway Timeout`
- **수용 기준 (AC)**:
  - 백엔드 2대를 띄우고 `/api/test`를 4회 요청 시 1→2→1→2 순으로 분산된다.
  - 백엔드 중 1대가 죽었을 때 클라이언트에 `502`가 반환되고 프록시 서버 자체는 크래시하지 않는다.

### FR-08. Forward Proxy (선택 / 확장)
- **명세**:
  - 프록시 모드로 들어온 요청(`GET http://example.com/path HTTP/1.1`)의 호스트와 포트를 파싱하여 `getaddrinfo()`로 DNS 조회 후 원 서버에 TCP 연결해 요청/응답을 중계한다.
  - (선택) 동일 `GET` 요청에 대해 TTL 기반 인메모리 캐싱(`Cache Hit` 시 원 서버 연결 생략).
- **수용 기준 (AC)**:
  - `curl -x http://127.0.0.1:<port> http://httpbin.org/get` 요청 시 정상 응답을 반환한다.

---

## 5. 비기능 요구사항 (Non-Functional Requirements)

### NFR-01. 메모리 안전성 및 누수 방지
- 모든 동적 메모리(`malloc`/`calloc`/`realloc`)는 요청 처리 완료 또는 오류 발생 시 반드시 `free`되어야 한다.
- AddressSanitizer(`-fsanitize=address,undefined`) 및 Valgrind 검사에서 **메모리 누수 0 bytes, 잘못된 메모리 접근 0건**을 달성한다.

### NFR-02. 시그널 및 Graceful Shutdown
- 클라이언트 소켓 쓰기 중 연결 끊김으로 프로세스가 죽지 않도록 시작 시 `SIGPIPE`를 무시(`SIG_IGN`)한다.
- `SIGINT`, `SIGTERM` 수신 시 새로운 연결 수락을 중단하고, 처리 중인 워커/소켓 자원을 안전하게 정리한 뒤 종료 코드 `0`으로 종료한다.

### NFR-03. 타임아웃 및 연결 수명 관리
- 클라이언트 읽기/쓰기 타임아웃(예: `5초`), Keep-Alive 유휴 타임아웃(예: `5초`), 백엔드 프록시 연결/응답 타임아웃(예: `5초`)을 적용해 무한 블로킹을 방지한다.
- `Connection: keep-alive` 지원 시, 단일 연결당 최대 요청 수(예: `100`회) 또는 유휴 타임아웃 도달 시 서버가 연결을 닫는다.

### NFR-04. 로깅 및 관측성
- 모든 요청에 대해 `[시간] [스레드/워커ID] [클라이언트 IP] "METHOD URI" STATUS BYTES LATENCY_MS` 형식의 접근 로그를 stdout/stderr에 스레드 안전하게 출력한다.

---

## 6. 엣지 케이스 및 예외 처리 매트릭스
| 케이스 | 기대 동작 |
|---|---|
| 불완전한 HTTP 요청 (헤더 끝 `\r\n\r\n` 없이 EOF 또는 타임아웃) | `400 Bad Request` 또는 연결 종료 (서버 블로킹 없음) |
| 지원하지 않는 메서드 (`DELETE`, `FOOBAR` 등) | `405` 또는 `501` 반환 후 연결 정리 |
| URI 길이 > 2048 bytes | `414 URI Too Long` 반환 |
| Body 크기 > 1MB (`Content-Length` 기준) | `413 Payload Too Large` 반환 후 연결 종료 |
| `POST`인데 `Content-Length` 누락 | `411 Length Required` 또는 `400 Bad Request` |
| `../../etc/passwd` 접근 시도 | `403 Forbidden` 반환 |
| 전송 도중 클라이언트가 강제 종료 (`RST` / `EPIPE`) | 서버 크래시 없이 소켓/메모리 회수 |
| Reverse Proxy 백엔드 서버 미구동 | `502 Bad Gateway` 반환 |
| Reverse Proxy 백엔드 응답 지연 (> 타임아웃) | `504 Gateway Timeout` 반환 |

---

## 7. 테스트 및 데모 검증 기준 (Verification)
목요일 발표(README.md 기준)에 포함할 검증 항목은 다음과 같다.
1. **단위 테스트 (`make test`)**: HTTP 요청 라인/헤더 파서, 경로 보안 검사, MIME 매핑, Round-Robin 선택 로직.
2. **기능 테스트 (`tests/integration/`)**: `curl` 또는 Python `pytest` 스크립트로 정적 파일(200/403/404), POST, Keep-Alive, 내장 동적 엔드포인트, Reverse Proxy 라우팅/로드밸런싱/502·504 자동 검증.
3. **메모리 검사 (`make asan` / `valgrind`)**: 통합 테스트 시나리오 전체 실행 후 누수 0건 확인.
4. **부하 테스트**: `wrk` 또는 `hey`/`ab`로 동시 접속(예: `-c 50 -t 4 -d 10s`) 실행 후 RPS, 평균 지연시간, Non-2xx 에러 수 기록.