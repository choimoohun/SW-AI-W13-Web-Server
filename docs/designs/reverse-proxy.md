# 7단계 리버스 프록시 (Reverse Proxy)

* **Author:** choimoohun
* **Date:** 2026-11-25

## Overview

기존 6단계 스레드 풀 웹 서버(`httpd`, 워커 8개, 연결 대기열 16칸, 블로킹 RIO, C17 / `_POSIX_C_SOURCE=200809L`) 위에 **URI 경로 접두사 기반 리버스 프록시**를 추가합니다. 실행 시 CLI 인자로 `<접두사>=<호스트:포트>[,<호스트:포트>...]` 형식의 라우팅 규칙을 받아, 일치하는 요청은 백엔드 목록에 스레드 안전한 단순 라운드로빈으로 전달하고 일치하지 않는 요청은 기존 `/cgi-bin/` CGI 및 정적 파일 서빙으로 넘깁니다.

프록시 경로는 `GET`과 `POST`만 허용하며, 클라이언트↔프록시와 프록시↔백엔드 양쪽 모두 `Connection: close`를 사용해 커넥션 풀과 청크 디코더 없이 단순하게 동작합니다. 백엔드 연결 실패나 비정상 응답은 즉시 `502`, 시간 초과는 즉시 `504`를 반환하고 자동 재시도는 하지 않습니다.

## Analysis

7개 독립 Goldfish 리뷰어(`egm-design`, `egm-codebase-details`, `egm-zoom-out`, `egm-comprehension`, `egm-implementation-readiness`, `egm-structural`, `egm-inventive-catalyst`)가 현재 코드베이스를 직접 검사해 도출한 핵심 모순, 위험, 해결 패턴입니다.

### 1. 핵심 모순과 해결 패턴 (TRIZ)

1. **청크 디코더 배제 vs. HTTP/1.1 백엔드 호환 및 `502`/`504` 정확성**
   - **충돌:** RFC 9110 §7.6.1은 `Transfer-Encoding`을 hop-by-hop 헤더로 분류합니다. 그런데 Go·Node·Flask 같은 HTTP/1.1 백엔드는 `Connection: close`를 받아도 `Transfer-Encoding: chunked`를 보낼 수 있습니다. 프록시가 디코딩 없이 `Transfer-Encoding` 헤더만 제거하고 본문을 그대로 흘리면 클라이언트가 깨진 청크 프레임을 본문으로 받습니다.
   - **해결 (상위 프로토콜 다운그레이드 + Fail-Closed):** 백엔드로 보내는 요청 줄에 `HTTP/1.0` + `Host:` + `Connection: close`를 사용합니다(nginx 기본 방식). RFC 9112 §6.1상 HTTP/1.1 서버는 `HTTP/1.0` 요청에 청크 인코딩이나 `100 Continue`를 보낼 수 없습니다. 규격을 어기고 `Transfer-Encoding`이나 `1xx`를 보내는 백엔드는 응답 헤더 전송 전에 차단하고 `502`를 반환합니다.
2. **고정 크기 `http_request` 유지 vs. 임의 요청 헤더 전달 및 hop-by-hop 제거**
   - **충돌:** [`src/http_request.h`](../../src/http_request.h)는 `Host`, `Connection`, `Content-Length`만 저장하고 `Content-Type`, `Authorization`, `Cookie` 등을 버립니다. 여기에 동적 헤더 배열을 추가하면 정적·CGI 경로까지 메모리 관리 복잡도가 늘어납니다. 또한 `req.connection[32]`는 31바이트 초과 `Connection:` 헤더를 400으로 거부하며 중복 `Connection:` 줄을 덮어씁니다([`src/http_request.c`](../../src/http_request.c)).
   - **해결 (기존 스택 버퍼 재사용):** [`src/httpd.c`](../../src/httpd.c)의 `handle_connection()`은 이미 CRLF와 NUL 검사가 끝난 원본 헤더 블록 `buf[MAXLINE]`(`len`바이트)을 스택에 들고 있습니다. `http_request`를 힙 구조로 바꾸지 않고 `(buf, len)`을 `proxy_serve()`에 그대로 넘겨, `buf`에서 정적 hop-by-hop 헤더와 모든 `Connection:` 줄에 열거된 동적 헤더 이름을 걸러낸 뒤 재조합합니다.
3. **응답 검증(`502`/`504`) vs. `O(1)` 메모리 스트리밍 및 `rio_readnb` 정지 방지**
   - **충돌:** 응답 전체를 [`src/cgi.c`](../../src/cgi.c)처럼 1 MiB 버퍼에 모으면 큰 응답이 깨지고 워커 8개 기준 힙이 16 MiB까지 늡니다. 반대로 첫 바이트부터 바로 흘리면 깨진 응답 헤더를 `502`로 바꿀 수 없습니다. 또한 [`src/http_request.c`](../../src/http_request.c)의 `http_read_head()`는 EOF(`502`)와 타임아웃(`504`)을 모두 `HTTP_READ_CLOSED`로 뭉개고, `rio_read()`는 EOF에서 `errno`를 지우지 않아 직전 `EAGAIN`을 오판할 수 있습니다.
   - **해결 (헤더 버퍼링 + 본문 스트리밍 2단계 전환):**
     1. 백엔드 응답 헤더만 `\r\n\r\n`까지 버퍼링·검증합니다(`errno = 0` 초기화 후 EOF→`502`, `EAGAIN`/`EWOULDBLOCK`→`504`, 형식 오류·헤더 크기 초과→`502`).
     2. 헤더를 클라이언트에 보낸 뒤에는 `up_rio.rio_buf`에 미리 읽혀 있던 본문 앞조각을 즉시 내보내고, 이후는 `rio_readnb()` 대신 `read(upfd, ...)` → `rio_writen(connfd, ...)` 고정 8 KiB 루프로 스트리밍합니다.
     3. 백엔드 응답에 유효한 `Content-Length`가 있으면 정확히 그 바이트 수만큼 읽고 `upfd`를 즉시 닫습니다(`204`/`304`는 0바이트). 이렇게 하지 않으면 `Connection: close`를 무시하는 백엔드에서 매 요청마다 5초 타임아웃에 걸립니다.
4. **고정 5초 타임아웃 vs. 빠른 CTest 단위·통합 검증**
   - **해결 (타임아웃 주입):** `proxy_config` 구조체에 `timeout_ms`(기본값 `5000`)를 두고 `tests/test_proxy.c`에서는 `100~200ms`로 낮춰 실제 루프백 소켓의 `502`/`504` 경로를 1초 미만으로 검증합니다.

### 2. 코드베이스·POSIX.1-2008 제약에서 나온 주요 위험

- **`csapp.c`의 `open_clientfd()` / `Open_clientfd()` 사용 금지:** [`src/csapp.c`](../../src/csapp.c)의 `open_clientfd()`는 (1) 블로킹 `connect()`에 타임아웃이 없어(Linux SYN 재시도 ~63~127초) 워커 8개를 고갈시키고 `504`를 못 내며, (2) `FD_CLOEXEC` 없이 소켓을 열어 동시 CGI `fork()`에 백엔드 소켓이 새며, (3) `close()`/`freeaddrinfo()`가 `errno`를 덮어써 `502`/`504` 구분을 망가뜨립니다.
- **POSIX.1-2008 (`_POSIX_C_SOURCE=200809L`, `CMAKE_C_EXTENSIONS OFF`) 호환:** [`CMakeLists.txt`](../../CMakeLists.txt) 제약 때문에 `SOCK_CLOEXEC`, `SOCK_NONBLOCK`, `accept4()`, `getopt_long()`은 컴파일되지 않습니다. 전용 `proxy_connect()`는 `socket()` → `fcntl(F_SETFD, FD_CLOEXEC)` → `fcntl(F_SETFL, O_NONBLOCK)` → `connect()` → `CLOCK_MONOTONIC` 기반 `poll(POLLOUT)` → `getsockopt(SO_ERROR)` → `O_NONBLOCK` 해제(해제하지 않으면 RIO가 `EAGAIN`으로 즉시 실패) → `SO_RCVTIMEO`/`SO_SNDTIMEO` 순서로 작성합니다. CLI 파싱도 `getopt_long()` 대신 `argv[3..]` 단순 `strcmp()` 루프로 작성합니다.
- **접두사 경계와 경로 탐색 격리:** 단순 `strncmp(uri, "/api", 4)`는 `/apiv2`를 오매칭합니다. 최장 일치(longest prefix match) + 세그먼트 경계(`prefix` 끝이 `/`이거나 `uri[plen]`이 `\0`, `/`, `?`) 검사가 필요하며, 프록시 분기가 `static_resolve_path()`보다 앞에 오므로 `.`·`..`·`//` 조각 검사를 프록시 라우팅 전에도 적용해야 `/api/../admin` 우회를 막습니다.
- **요청 헤더 재작성 안전성:** [`src/http_request.c`](../../src/http_request.c)는 값이 같은 중복 `Content-Length`를 허용하고 중복 `Host`를 덮어씁니다. 프록시는 중복 `Content-Length`를 단일 정규 값으로 출력하고(빈 `POST`에는 `Content-Length: 0` 명시), 요청 헤더 블록이 `MAXLINE`에 가까울 때 `Connection: close`나 `Host:` 추가로 버퍼가 넘치지 않도록 여유 공간(`MAXLINE + 1024`) 또는 줄 단위 송신을 써야 합니다.
- **`accept()`의 `EMFILE`/`ENFILE` 처리:** [`src/httpd.c`](../../src/httpd.c)는 현재 `EMFILE` 시 `unix_error()`로 서버를 종료합니다. 단순히 `continue`만 넣으면 커널 큐에 대기 연결이 남아 CPU 100% 무한 루프가 되므로, `stderr` 경고 후 짧은 `nanosleep`(예: 50ms)을 두고 `stop`을 확인하며 재시도합니다.
- **워커 8개 풀 고갈과 장애 관측성:** 워커 8개가 공유 풀 하나를 쓰므로 응답 없는 백엔드 1대에 요청이 몰리면 정상 백엔드와 정적·CGI까지 막힐 수 있습니다(현재 스레드 풀 모델의 구조적 한계로 문서화). 또한 현재 [`src/httpd.c`](../../src/httpd.c)는 `METHOD URI VERSION`만 출력해 `502`/`504` 원인을 알 수 없으므로, 프록시 오류 및 헤더 송신 후 중단에 `backend`, `phase`, `errno`를 한 줄 `fprintf(stderr, ...)`로 남깁니다.
- **모듈 경계와 문서 동기화:** [`src/httpd.c`](../../src/httpd.c)를 얇게 유지하고 라우팅·헤더 필터링·연결·중계 로직은 정적 라이브러리 `http` 안의 `src/proxy.h`/`.c`로 분리해 `tests/test_proxy.c`에서 단위·루프백 테스트합니다. [`src/http_response.c`](../../src/http_response.c)에는 `502`/`504`만 추가합니다.

## Goals vs. Non-Goals

* **In-Scope Goals:**
  * URI 경로 접두사 기반 리버스 프록시 라우팅(최장 세그먼트 경계 일치)과 기존 `/cgi-bin/`·정적 파일 폴백 유지
  * 기존 `./httpd <port> <root>`와 하위 호환되는 CLI 라우팅 인자 추가(잘못된 인자 시 non-zero 종료)
  * 프록시 경로의 `GET`/`POST` 지원(그 외 메서드 `501`), 경로별 스레드 안전한 단순 라운드로빈(`pthread_mutex_t`)
  * 프록시↔백엔드 `Connection: close` 기반 헤더 버퍼링·본문 스트리밍 중계와 hop-by-hop 헤더 제거
  * 백엔드 연결 실패·비정상 응답 즉시 `502`, 연결·송수신 시간 초과 즉시 `504`(재시도 없음)
  * 5~6단계 경계 보강: 백엔드 소켓 `FD_CLOEXEC`, `http_status_text()`의 `502`/`504` 추가, `accept()` `EMFILE`/`ENFILE` 비종료·비회전 방어, [`docs/reviews/stage-1-4-deferred-decisions.md`](../reviews/stage-1-4-deferred-decisions.md) 결정 C(`OPTIONS *`) 정리
  * CTest 등록 단위·루프백 통합 테스트(`tests/test_proxy.c`) 및 저장소 설계 문서 동기화
* **Out-of-Scope Non-Goals:**
  * 부하 테스트 튜닝 및 C10K(이벤트 루프 전환, `epoll`/`poll` 전면 재작성) — 과제 품질 항목의 `ab`/`wrk` 단순 측정은 7단계 서버 기능이 아닌 별도 검증 단계로 분리
  * 클라이언트↔프록시 및 프록시↔백엔드 keep-alive, 백엔드 커넥션 풀
  * Forward Proxy 및 응답 캐싱
  * `Transfer-Encoding: chunked` 디코딩(요청·응답 모두)
  * 자동 장애 우회(failover retry), 헬스 체크, 가중치 로드 밸런싱
  * 설정 파일 파서(1차 범위 제외, 동일 규칙 파서로 추후 확장만 열어 둠), `Host` 기반 라우팅, HTTPS/TLS, WebSockets, `GET`/`POST` 외 메서드

## Alternatives Weighed

### 1. 시작 시 라우팅 규칙 입력 방식: CLI 인자 vs. 설정 파일

| 비교 기준 | CLI 인자 (채택안: `./httpd <port> <root> [--route <prefix>=<h:p>[,...]]...`) | 설정 파일 전용 (`./httpd -c httpd.conf`) | CLI + 선택적 설정 파일 (`--config <file>`) |
| --- | --- | --- | --- |
| 1~6단계 하위 호환 | `./httpd <port> <root>` 그대로 동작 | 기존 실행 방식·문서·테스트 스크립트와 충돌 | 하위 호환 유지 |
| CTest·Docker 실행 편의 | 임시 파일 없이 한 줄로 실행·검증 가능 | 테스트마다 임시 설정 파일 생성·정리 필요 | 테스트는 편하지만 파일 테스트 추가 필요 |
| C17/POSIX 파서 복잡도 | `argv[3..]` 단순 `strcmp` 루프(~25줄). GNU `getopt_long` 불필요 | 파일 열기, 줄 길이 제한, 주석·공백, 줄 번호 오류 처리 필요 | 파일 파서 부담 동일 |
| 규칙 10개 이상 가독성 | 명령줄이 길어지고 주석 불가 | 가독성과 버전 관리에 유리 | 가독성 유리 |
| 과제 범위 적합성 | 과제 요구사항("CLI 인자 또는 설정 파일") 충족, 최소 범위 | 리버스 프록시 핵심과 무관한 파일 문법 설계가 늘어남 | 요구사항 초과 |

* **선택 근거:** 과제 규모(라우트 2~5개, Docker/CTest 반복 실행)에서는 CLI 인자가 파서 크기, 하위 호환성, 테스트 단순성 모두에서 유리합니다. 단, 규칙 문자열 파싱을 순수 함수 `proxy_router_add_spec(router, "<prefix>=<h:p>,...")`로 분리해 두면 추후 `--config <file>`이 필요할 때 파일의 각 줄을 같은 함수에 넘기는 15줄만 추가하면 됩니다.

### 2. 기타 검토 후 기각한 대안

* **프록시↔백엔드 keep-alive / 커넥션 풀** — *작성자 기각:* 백엔드별 유휴 소켓 풀 동기화, chunked/Content-Length 정밀 프레이밍, 끊긴 유휴 소켓에서의 `POST` 비멱등 재시도 금지 등 복잡도가 크고 클라이언트 구간도 `Connection: close`라 실익이 적음.
* **이벤트 루프(`poll`/`epoll`)로 6단계 동시성 재작성** — *작성자 기각:* 기존 블로킹 RIO 파서·정적·CGI 코드를 전면 재작성해야 하며, 현재 단계에서는 기존 스레드 풀 위에 리버스 프록시를 얹고 부하 테스트(C10K)는 보류함.
* **`csapp.c`의 `open_clientfd()` 재사용** — *Goldfish(`egm-design`, `egm-codebase-details`, `egm-structural`, `egm-implementation-readiness`, `egm-inventive-catalyst`) 경고로 기각 (`egm-zoom-out`은 연결 타임아웃 테스트가 없으면 재사용 가능 의견 제시):* `open_clientfd()`는 무제한 블로킹 `connect()`(약 127초 SYN 재시도), `FD_CLOEXEC` 누락, `errno` 손실이 있어 8워커 풀 고갈과 `504` 실패를 유발함.
* **백엔드 응답을 헤더 파싱 없이 첫 `read()`부터 원문 그대로 스트리밍 (`egm-zoom-out` 대안)** — *기각(부분 채택):* 구현은 가장 짧지만, 깨진 응답 상태 줄에 `502`를 못 주고 `Connection: close`를 무시하는 백엔드에서 `Content-Length` 뒤 5초간 멈추며 백엔드 hop-by-hop 헤더를 거르지 못함. 대신 헤더만 버퍼링하고 본문은 스트리밍하는 절충안을 채택함.
* **백엔드 응답 전체 버퍼링 (`cgi.c` 방식 1 MiB 상한)** — *기각:* 큰 응답을 서빙할 수 없고 워커당 힙 사용량이 두 배로 늘어남.
* **백엔드 장애 시 다음 라운드로빈 백엔드로 자동 재시도** — *작성자 기각:* `POST` 중복 실행 위험과 타임아웃 누적 지연을 피하고 단순 라운드로빈 + 즉시 `502`/`504`로 범위를 고정함.
* **HTTP/1.1 백엔드에 청크 디코더 직접 구현** — *작성자 기각:* 작업 범위를 줄이기 위해 백엔드에 `HTTP/1.0` + `Connection: close`를 보내 청크 응답을 원천 차단하고, 비규격 청크 응답은 `502`로 거부함.

## Key Decisions & Staging for Next Steps

### 확정된 아키텍처 및 경계 결정

1. **모듈 구조 및 용량:** `src/proxy.h`, `src/proxy.c`를 정적 라이브러리 `http`에 추가하고 `tests/test_proxy.c`를 CTest에 등록합니다. `src/httpd.c`는 CLI 파싱·초기화·분기와 `EMFILE` 방어만 담당합니다. 라우트·백엔드 저장소는 [`src/conn_queue.h`](../../src/conn_queue.h)와 같은 고정 배열 상한(`PROXY_MAX_ROUTES = 16`, `PROXY_MAX_BACKENDS = 16`)을 사용해 종료 시 힙 정리 복잡도를 없앱니다.
2. **CLI 입력과 오류 종료:** `./httpd <port> <root> [--route <prefix>=<host:port>[,<host:port>...]]...` 형식을 `argv[3..]` 단순 `strcmp` 루프로 파싱합니다(GNU `getopt_long` 미사용). 잘못된 인자, 중복 접두사, 잘못된 포트(`1..65535`), 시작 시 DNS 해석 실패는 `stderr`에 원인을 출력하고 `exit(1)`로 즉시 종료합니다. 규칙 문자열 파싱은 순수 함수 `proxy_router_add_spec()`으로 분리합니다.
3. **DNS 해석 시점:** 백엔드 호스트·포트는 서버 시작 시 `getaddrinfo()`로 1회 해석해 `struct sockaddr_storage`와 `socklen_t`를 캐시합니다. 시작 시 오타를 즉시 잡고, 요청 처리 중 타임아웃 없는 DNS 조회로 워커가 묶이는 것을 막습니다(백엔드 IP 변경 시 서버 재시작 필요).
4. **네트워크 연결 (`proxy_connect`):** `csapp.c`의 `open_clientfd()`를 쓰지 않고 전용 POSIX.1-2008 연결 함수를 구현합니다(`socket()` → `fcntl(FD_CLOEXEC)` → `O_NONBLOCK` → `connect()` → `CLOCK_MONOTONIC` 기반 `poll(POLLOUT)` → `getsockopt(SO_ERROR)` → `O_NONBLOCK` 해제 → `SO_RCVTIMEO`/`SO_SNDTIMEO`). 타임아웃은 `proxy_config.timeout_ms`(기본 `5000`, CTest에서는 `100~200`)로 주입합니다.
5. **라우팅 규칙, 접두사 처리 및 우선순위:**
   - 접두사는 `'/'`로 시작해야 하며, 루트 `'/'`를 제외한 끝 `'/'`는 정규화해 제거합니다(예: `/api/`와 `/api`는 동일 규칙으로 중복 거부).
   - **최장 세그먼트 경계 일치**를 적용해 `/api`가 `/api`, `/api?...`, `/api/...`만 매칭하고 `/apiv2`는 매칭하지 않게 합니다.
   - **원본 URI+쿼리 그대로 유지:** `/api/users?a=1`을 백엔드에도 `/api/users?a=1`로 전달합니다(접두사 제거 시 발생하는 빈 경로·리다이렉트 불일치 배제).
   - **분기 우선순위:** 프록시 라우트 → `/cgi-bin/` → 정적 파일. `.`·`..`·`//` 경로 조각 거부(`403`)는 프록시 라우팅 전에도 공통 적용하고, `GET`/`POST` 허용 검사는 본문 할당·백엔드 연결 전에 수행합니다.
6. **요청·응답 중계 및 헤더 정책:**
   - `handle_connection()`의 원본 헤더 버퍼(`buf`, `len`)에서 정적 hop-by-hop 헤더와 모든 `Connection:` 줄에 열거된 동적 헤더를 제거한 뒤 `HTTP/1.0`, `Host: <backend_host>:<backend_port>`, `Connection: close`, 단일 `Content-Length`(빈 `POST`는 `0`)를 붙여 전달합니다.
   - `http_request.connection` 버퍼는 `256`바이트로 확장해 정상적인 다중 토큰 `Connection:` 헤더가 400으로 거절되지 않게 합니다([`src/http_request.h`](../../src/http_request.h), [`tests/test_http_request.c`](../../tests/test_http_request.c) 동시 갱신).
   - 백엔드 응답은 헤더 전체를 검증한 뒤 클라이언트에 전송하고(`Transfer-Encoding`·`1xx`·형식 오류·EOF는 `502`, 시간 초과는 `504`), 본문은 `up_rio.rio_buf` 잔여 바이트를 비운 뒤 `read()` → `rio_writen()`으로 스트리밍합니다(`Content-Length` 존재 시 정확히 그 바이트만 읽고 즉시 `close`, `204`/`304`는 0바이트).
7. **동기화, 경계 보강 및 상태 텍스트:**
   - 라우트별 `pthread_mutex_t`로 단순 라운드로빈 인덱스를 보호하고 종료 시 `pthread_mutex_destroy()`로 해제합니다.
   - [`src/http_response.c`](../../src/http_response.c)에 기존 문장형 표기와 맞춰 `502` (`"Bad gateway"`), `504` (`"Gateway timeout"`)를 추가합니다.
   - [`src/httpd.c`](../../src/httpd.c)의 `accept()`에서 `EMFILE`/`ENFILE` 시 `stderr` 경고와 50ms `nanosleep` 백오프로 크래시와 CPU 100% 회전을 방지합니다.
   - [`docs/reviews/stage-1-4-deferred-decisions.md`](../reviews/stage-1-4-deferred-decisions.md) 결정 C에 따라 `OPTIONS *`만 `http_parse_head()`에서 허용해 연결 처리에서 `501`로 응답하고(`GET *` → `400`), `absolute-form`/`authority-form`은 리버스 프록시 범위 외로 `400`을 유지합니다.
   - 프록시 오류(`502`/`504`) 및 헤더 송신 후 중단은 `backend`, `phase`, `errno`를 한 줄 `fprintf(stderr, ...)`로 기록합니다.
8. **문서 동기화 대상 (`AGENTS.md` 준수):** `docs/reverse-proxy/{design,functions,test-cases}.md` 신규 작성, [`docs/README.md`](../README.md), [`docs/static-file-serving/design.md`](../static-file-serving/design.md), [`docs/http-request-parsing/design.md`](../http-request-parsing/design.md), [`docs/http-response/design.md`](../http-response/design.md), [`docs/concurrency/design.md`](../concurrency/design.md), [`docs/dynamic-request/design.md`](../dynamic-request/design.md), [`docs/reviews/stage-1-4-deferred-decisions.md`](../reviews/stage-1-4-deferred-decisions.md).
