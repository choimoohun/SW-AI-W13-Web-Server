# 설계 검증 보고서  
> 이 문서는 초기 설계 시점(2026-11-21)의 검토 기록입니다. 현재 구조는 `docs/DESIGN.md`의 "현행 구조 메모" 절을 기준으로 보세요.

- 검토 기준: `docs/REQUIREMENTS.md`, `docs/DESIGN.md`, `docs/iterations/STEP1~4`, `docs/DECISIONS.md`, `include/`·`src/` 스텁, 빌드/설정 파일 (2026-11-21 세션)
- 포함 범위: TCP 연결 수락 → HTTP 요청 읽기·파싱 → 정적 파일 찾기 → HTTP 응답 전송 → 연결 정리
- 제외 범위: Reverse/Forward Proxy, CGI (오늘 검토 대상 아님)
- 동시성(FR-06): 오늘 핵심 흐름과 분리하여 §4에서 별도 판정
- 표기: [사실] = 파일에서 확인된 내용, [미정] = 문서가 확정하지 않은 부분, [제안] = 검토자 권고

---

## 모듈 역할과 호출 순서

### 현재 스텁이 정의한 흐름

```text
main.c            설정 준비 + 시그널 정책 + 모듈 조립만 (소켓/파싱 직접 구현 안 함)
  └─ tcpserver.c      getaddrinfo → socket → SO_REUSEADDR → bind → listen → accept
       └─ connection.c   fd별 수명: 읽기 누적 → 파서 호출 → 분기 → 응답 후 정리
            ├─ http_parser.c     순수 파서 (소켓 IO 없음, 버퍼+길이 입력, NUL 가정 안 함)
            ├─ static_handler.c  URI + doc_root → 파일 응답 또는 403/404/500
            └─ http_response.c   상태줄+헤더 직렬화, 부분 전송 반복, 바이너리 안전
       └─ connection.c   정리 (버퍼/fd 단일 지점 해제)
```

### 판정: 자연스럽고 올바름 [사실]

순환 의존이 없는 계층 구조이며, 스텁 주석이 핵심 원리를 이미 정확히 기술하고 있다:

| 원리 | 근거 |
|---|---|
| TCP는 메시지 경계가 없는 바이트 스트림 → read 경계 ≠ 요청 경계 | `src/http_parser.c` 주석 3~4행 |
| short read/write 반복 처리, EINTR/EPIPE 구분 | `src/http_response.c` 주석 6~8행 |
| Content-Length는 문자 수가 아니라 바이트 수 | `src/http_response.c` 주석 4~5행 |
| 본문은 포인터+길이로 다뤄 바이너리 보존 | `src/static_handler.c`, `include/http_response.h` |
| 청소는 단일 지점, fd는 -1 초기화, SIGPIPE 무시 | `docs/DESIGN.md` §5 |
| Keep-Alive 미지원 단계에서는 Connection: close 명시 후 종료 | `src/connection.c` 주석 7~8행 |

이 주석들은 발표 시 동작 원리 설명으로 그대로 사용할 수 있는 수준이다.

### 결함: 큰 파일 청크 전송의 소유권이 비어 있음 [미정]

- `src/http_response.c` 주석은 "직렬화와 전체 전송"을 담당한다고 하고, `src/static_handler.c`는 파일 열기/크기 확인을 담당한다고 하지만, 둘 사이 인터페이스가 어느 문서에도 정의되어 있지 않다.
- `STEP03_HTTP_RESPONSE.md` 47행, `STEP04_STATIC_FILE.md` 41행 모두 "인터페이스는 구현하면서 정합니다"로 비워 두었다.
- 대용량 파일(DESIGN.md §4.6①의 100MB 예시)은 본문 버퍼에 통째로 담을 수 없으므로, 파일 fd와 전송 offset 상태를 누가 소유하는지반드시 고정해야 한다.

[제안] 소유권 계약:
1. `static_handler`는 경로 검증·stat·open까지 수행하고 `{status, mime, file_fd, size}`를 반환한다.
2. `http_response`는 헤더 전송 후 `read(file_fd, 16KB) → send_all(client_fd)` 루프를 돈다.
3. `file_fd`/`client_fd` 닫기는 `connection`의 단일 정리 지점에서만 수행한다.

### router 부재 [제안]

오늘은 `connection`이 `static_handler`를 직접 호출하면 된다. `DESIGN.md` §4.5의 router는 프록시/CGI 단계에서 도입한다고 문서에 표시만 해 두면 자연스럽다.

---

## 문서 ↔ 실제 트리: 이름 불일치와 누락 (확인된 사실)

### 이름 불일치

| 문서가 가리키는 이름 | 실제 트리 | 근거 |
|---|---|---|
| `include/server.h`, `src/server.c` | `include/tcpserver.h`, `src/tcpserver.c` | STEP1 '관련 모듈' 표 |
| `conf/server.conf` | `conf/tcpserver.conf` | DESIGN.md §3 파일 지도 |
| `log.h`, `reactor.h`, `router.h`, `cgi_handler.h`, `proxy_handler.h`, `cache.h` | 트리에 없음 | DESIGN.md §3 (오늘 범위 논점은 reactor/router/log뿐, 나머지는 제외 범위) |

### 누락/빈 파일

| 항목 | 상태 | 영향 | 근거 요구사항 |
|---|---|---|---|
| `include/tcpserver.h` | [치명] 시스템 헤더가 이미 정의하는 `struct sockaddr_in`/`struct sockaddr`를 재정의하고, 내용도 문법 오류(`uint16_t uint16_t sin_port;` 타입 중복, `sin_zero[8];` 타입 없음, `sin_family` 타입 오류). include 시 컴파일 에러. | 빌드 불가 | 모듈 헤더는 자기 API 선언만 가져야 함 |
| `Makefile` | [치명] 1줄 주석뿐, 타겟 없음 | 빌드/테스트 불가 | C-04 (`make`, `make test`, `make asan`, `make clean`) |
| `CMakeLists.txt` | 소스/실행 파일 타겟 없음 | 아무것도 빌드되지 않음 | C-04와 병행 정리 필요 |
| `conf/tcpserver.conf` | 0바이트 | 포트/doc_root 주입 불가 | C-05 |
| `src/config.c` | 없음 (`config.h`만 존재) | 설정 모듈 미구현 상태가 문서에 명시 안 됨 | C-05 |
| `src/common.c` | 0바이트 | 헤더 전용 상수 모듈로 두고 .c는 빌드에서 제외하거나 삭제 권장 | — |
| `www/index.html` | 내용이 `# 기본 웹 페이지` 22바이트, HTML 아님 | "/"→index.html 데모가 마크다운 텍스트로 노출 | FR-04 |
| 바이너리 테스트 픽스처(png/jpg) | `www/`에 없음 | 바이너리 무결성 AC 증명 불가 | FR-04 AC |
| `tests/` 디렉터리 | 없음 | 단위/기능 테스트없음 | REQUIREMENTS §7, DESIGN §3 |

### 확인 후 기각된 의심

- 파일명 후행 공백 의심(`find` 출력에서 `connection.h `, `http_parser.h `로 보였으나, `read_file`/`search_files` 확인 결과 7개 헤더 모두 정상 이름. 문제 없음.

---

## 경로 탈출(Path Traversal) 방어 검증

### 설계 방향은 올바름 [사실]

- `DESIGN.md` §4.6①: 퍼센트 디코딩 → `..` 차단 → `realpath`로 doc_root 내부인지 2차 확인.
- `src/static_handler.c` 주석: query string 분리, "단순한 부분 문자열 검사만 보안 경계로 삼지 않는다", 심볼릭 링크 고려, doc_root는 설정에서 받음.
- 판정: 이 방향대로 구현하면 탈출 차단 가능. [조건부 충족]

### 경로 검증 순서 [확정]

`STEP04_STATIC_FILE.md`에 순서가 확정되어 있고, 아래 함정을 구현에 반영했다:

1. `..` 차단은 부분 문자열이 아니라 경로 컴포넌트 단위여야 한다 (`/a..b`는 정상 파일명).
2. `realpath` 결과 비교는 디렉터리 경계 포함: `resolved == root` 또는 `root + "/"`로 시작. 단순 `strncmp(root)`면 doc_root가 `/www`일 때 `/www-evil`이 통과한다.
3. `realpath`는 존재하지 않는 경로에서 NULL 반환 → errno 매핑 필요: `ENOENT→404`, `EACCES→403`, 기타→500. 탈출 시도는 대상이 존재해도 403.
4. 심볼릭 링크: www 안의 링크가 밖을 가리켜도 `realpath`가 실제 경로를 풀므로 경계 검사에서 걸림 → 403. 문자열 검사만으로는 못 막는 부분이며, 스텁 주석의 방향이 옳은 이유.
5. 디코딩 정책: 1회 디코딩 후 검증, 이중 인코딩(`%252e`)·NUL 바이트는 400 거부 명시.
6. doc_root는 시작 시 1회 `realpath`해서 보관 (요청마다 수행하지 않음).

### [제안] 고정 알고리즘 순서

```text
query/fragment 제거
  → 퍼센트 디코딩(정책: 1회, 제어문자/NUL은 400)
  → 컴포넌트 단위 ".." 거부 (403)
  → doc_root와 결합 (길이 검사)
  → realpath(결합 경로)
  → 경계 포함 prefix 검사 (밖이면 403)
  → stat/open → errno 매핑 (404/403/500)
```

- `/` → `doc_root/index.html`.
- 디렉터리에 index.html 없으면 403 (DESIGN.md와 일치). 디렉터리 목록 노출은 요구사항에 없으므로 추가하지 않는다.
### 파일 접근 보안 (Path Traversal 방지)
- 원리: 요청받은 경로를 실제 주소(Absolute Path)로 바꾼 뒤, 웹 서버의 루트 폴더를 벗어나는지 검사합니다.
- 구조:
  ```
  [ 요청: /../etc/passwd ] 
           |
           v
  [ 합체: /home/user/www/../etc/passwd ]
           | (realpath 함수 실행)
           v
  [ 실제: /etc/passwd ] <--- 루트(/home/user/www) 밖에 있으므로 거절(403)
  ```
- 참고: 검사 시점과 파일 열기 시점 사이의 보안 취약점은 현재 규모에서는 허용하기로 결정했습니다.

### 검증 벡터

`/../etc/passwd`, `/%2e%2e/etc/passwd`, `/a/../../etc/passwd`, 심볼릭 링크 탈출, `/www-evil`류 prefix, NUL 바이트, 2048 초과 URI(414), 없는 파일(404), 읽기 불가 파일(403).

---

## 동시성 — 별도 표시

### 문서 간 모순 [사실]

| 문서 | 내용 | 상태 |
|---|---|---|
| REQUIREMENTS FR-06 (82~89행) | Must(MVP에 동시성 포함), 안 A(thread pool) 권장 기본안 | 요구 |
| DESIGN.md §1 | poll 이벤트 루프 + fork 워커를 "선택한 방식"으로 서술 | 기정사실화 |
| `src/tcpserver.c` 주석 13~14행 | "thread pool 또는 poll 기반 처리를 결정해야 한다" | 미결정 |
| DECISIONS.md | 결정 기록 0건 | 미기록 |

아무 문서도 권위 있게 확정하지 못한 상태이며, 이 결정이 `connection.c`의 구조 전체(블로킹 직선 함수 vs 논블로킹 상태 머신)를 좌우한다.

### 오늘 범위에 대한 판정 [제안]

- 오늘 핵심 4가지 검증(/→index, 오류 응답, 바이너리 무결성, 탈출 차단)은 싱글스레드 + 블로킹 IO로 전부 달성 가능.
- 타임아웃은 `SO_RCVTIMEO`/`SO_SNDTIMEO` 5초로 설정하면 NFR-03 무한 블로킹 방지 만족.
- FR-06 AC(slow client 비차단, 동시 100+)는 Must이므로 프로젝트에서 제외 불가, 순서만 조정 가능.
- 권장 경로: 오늘 흐름을 "연결 하나를 블로킹으로 처리하는 자기완결 함수 `connection_handle(fd)`"로 구현 → 4가지 검증 통과 → accept 루프의 분기만 thread pool로 교체(핸들러 수정 없음). 안 A 채택 시 DESIGN.md §1 서술 수정 + DECISIONS.md D1 기록.
- poll 안(DESIGN 현황) 선택 시: 오늘 만든 블로킹 핸들러를 상태 머신으로 재작성해야 하고 reactor 모듈도 신규 필요 → 오늘 정적 파일 완주 위험 큼.
- Keep-Alive: 오늘 미지원이 맞음. 단 응답에 `Connection: close` 명시 후 요청 1건 처리 뒤 종료해야 한다(HTTP/1.1 기본이 keep-alive이므로 명시 없이 닫으면 클라이언트 재사용 시 오류). `src/connection.c` 주석에 이미 있음 — 정확.

---

## 오늘 핵심 4가지 설계 판정 요약

| 체크 | 설계 근거 | 판정 | 남은 조건 |
|---|---|---|---|
| "/" → index.html | DESIGN §4.6①, STEP4 | 충족 가능 | www/index.html을 실제 HTML로 교체 |
| 없음/접근불가 오류 응답 | static_handler.c 주석 7행 | 충족 가능 | errno→상태 매핑 표 고정 |
| 바이너리 무결성 | 포인터+길이 본문, 청크 전송, Content-Length=stat 크기 원칙 | 충족 가능 | 테스트용 이미지 픽스처 추가 |
| doc_root 밖 접근 차단 | DESIGN §4.6①, static_handler.c 주석 | 조건부 충족 | §3.3 고정 알고리즘 채택 |

---

## 우선순위

### 즉시 수정 (빌드/데모를 막는 확인된 결함)

1. `include/tcpserver.h` — 깨진 시스템 구조체 재정의 제거, 모듈 API 선언으로 교체
2. `Makefile` — `make` / `make test` / `make asan` / `make clean` 실제 타겟 작성 (C-04)
3. `conf/tcpserver.conf` — port, doc_root 최소 값 채움 (C-05)
4. `www/index.html` — 실제 HTML로 교체, 바이너리 테스트 픽스처 추가

### 오늘 결정해서 DECISIONS.md에 기록 (문서 간 모순 해소)

5. 동시성 안 A/B 확정 + DESIGN.md §1과 REQUIREMENTS FR-06 서술 일치
6. 파일 청크 전송 소유권 (static_handler → http_response 인터페이스, §1.3)
7. POST를 정적 경로에 요청했을 때 정책 (제안: 405, 파싱 자체는 FR-02대로 구현)
8. STEP1의 server.h/server.c → tcpserver.h/tcpserver.c 이름 수정

### 발표용 각주 보강 여지 (스텁 주석은 이미 우수, 아래만 추가)

- `tcpserver.h`: listen backlog=128의 의미(커널 접속 대기열), SO_REUSEADDR가 TIME_WAIT 재바인딩을 푸는 원리
- `static_handler`: realpath 경계 검사(prefix 함정)와 errno 매핑 근거
- `http_response`: 청크 전송 루프 소유권 계약
- `connection`: 동시성 결정 근거(왜 thread pool/poll인가)

---

## 적용 결과

- §6의 즉시 수정 항목을 파일에 반영했다. 이름 수정, 빈 파일 정리, 청크 전송 소유권, POST 405 정책이 실제 구현에 들어가 있다.
- 각주 보강은 `DESIGN_REVIEW`의 발표용 메모로 남기고, 코드 주석은 구현 단계에서 일부만 반영했다.

---

## 부록. getnameinfo 필요 여부 질의 응답

질의: `getnameinfo()` 선언을 프로젝트에 넣어야 하는가?

- 오늘 범위에서는 불필요. `getnameinfo`는 sockaddr → 호스트명/서비스 문자열 역변환 API다. 오늘 필요한 것은 `getaddrinfo`(문자열 주소/서비스 → 소켓 바인드용 주소 후보) 하나이며, 이는 tcpserver 설계에 이미 포함되어 있다.
- 용도가 클라이언트 IP 접근 로그(NFR-04)라면: IPv4만 지원할 경우 `inet_ntop()`으로 충분하다. IPv6까지 고려하면 `getnameinfo(..., NI_NUMERICHOST | NI_NUMERICSERV)`가 정석.
- 주의: 플래그 없이 `getnameinfo`를 호출하면 역DNS 조회를 수행하므로 블로킹된다. 요청 처리 경로에서는 금지.
- 붙여넣은 시그니처를 헤더에 선언으로 넣지 말 것. `<netdb.h>`가 이미 선언한다. 시스템 구조체/함수를 헤더에 재정의하다가 깨진 `include/tcpserver.h`와 같은 부류의 실수다.
