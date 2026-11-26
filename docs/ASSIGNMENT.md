# 과제 원문 — 웹 서버 만들기 (SW-AI W13)

> 크래프톤 정글 과제 안내를 옮겨 둔 것입니다. 요구사항 대비 구현 점검(`docs/records/audits/GAP-*.md`)의 질문 출처로 씁니다.
> 우리 요구사항 문서는 [REQUIREMENTS.md](REQUIREMENTS.md)입니다.

## 과제 개요
- C 언어로 동작하는 웹서버를 구현한다.
- 외부 HTTP 라이브러리·프레임워크 없이, POSIX 소켓 API만 사용하여 처음부터 직접 작성한다.
- 단순 HTTP 서버에서 끝나지 않고, Proxy 또는 Reverse Proxy 기능을 함께 구현한다.

## 필수 구현 항목
- **TCP 소켓 서버**
  - `socket() → bind() → listen() → accept()`의 표준 흐름을 직접 구현한다.
  - 클라이언트 연결을 수락하고 요청/응답을 송수신한다.
- **HTTP 요청 파싱**
  - 요청 라인(메서드, URI, 버전) 파싱
  - 헤더 파싱(Host, Content-Length, Connection 등)
  - 본문(body) 처리(POST 요청)
  - 최소 지원 메서드: GET, POST
- **HTTP 응답 생성**
  - 상태 라인(`HTTP/1.1 200 OK` 등)
  - 표준 헤더(Content-Type, Content-Length, Connection 등)
  - 본문 송신
- **정적 파일 서빙**
  - 지정된 루트 디렉터리에서 파일을 읽어 응답한다.
  - MIME 타입 매핑(HTML, CSS, JS, PNG, JPG 등)
  - 404 / 403 / 500 등 기본 오류 응답 처리
- **동적 요청 처리**
  - 요청에 따라 애플리케이션 로직을 실행하고 응답을 생성한다.
  - 예: CGI 프로그램 실행, 또는 백엔드 서버로 요청 전달
- **동시 요청 처리** — 다음 중 하나 이상
  - 멀티프로세스: `fork()`
  - 멀티스레드: pthread(요청 처리 스레드 풀 권장)
  - I/O 멀티플렉싱: `select()` / `poll()` / `epoll()` / `kqueue()` — 이벤트 루프로 여러 연결의 I/O를 관리한다.

## Proxy 또는 Reverse Proxy 구현 (필수)
- 둘 중 최소 한 가지(둘 다 구현하면 더 좋다).
- **Forward Proxy**
  - 클라이언트의 HTTP 요청을 받아 외부 서버에 대신 전달하고 응답을 그대로 돌려준다. (클라이언트 → Proxy → 원 서버)
  - 요청 URL의 호스트를 파악하여 새로운 TCP 연결을 만들고 데이터를 중계한다.
  - 선택: 캐싱
- **Reverse Proxy**
  - 외부 요청을 받아 내부의 여러 백엔드 서버로 라우팅한다. (클라이언트 → Reverse Proxy → 백엔드 1..N)
  - 가능하면 단순 로드 밸런싱(Round-Robin 등)까지 포함한다.
  - 라우팅 규칙은 경로(`/api/* → 8001`, `/static/* → 8002` …) 또는 호스트 헤더 기반으로 설계한다.
  - 정적 요청을 프록시가 직접 서빙할지, 백엔드로 전달할지는 구현자가 선택한다.

## 기술 스택 / 제한
- 언어: C
- 금지: libevent, libuv, nginx, Apache, mongoose 같은 완성된 HTTP / 이벤트 루프 라이브러리
- 허용: 표준 C 라이브러리, POSIX API(`<sys/socket.h>`, `<netinet/in.h>`, `<unistd.h>`, `<pthread.h>` 등)
- 빌드: Makefile 또는 CMakeLists.txt 제공
- 실행: 포트 번호와 정적 파일 루트(그리고 Reverse Proxy 라우팅 규칙)를 CLI 인자 또는 설정 파일로 받는다.

## 개발 환경
- macOS / Linux에서 개발 가능. Windows는 WSL2 + Ubuntu, Docker 또는 클라우드.
- 여러 환경(macOS / WSL / Linux)에서 빌드·실행하려면 OS별 차이를 고려해 이식성 있게 개발한다(POSIX 범위 안에 머무르거나 `#ifdef`로 분기).

## 중점 포인트
- 소켓 프로그래밍의 기본 흐름: 3-way handshake, `SO_REUSEADDR`, blocking / non-blocking 차이, bind 실패 / accept 블로킹 등의 동작 원리
- HTTP 명세(RFC 7230~7235)의 최소 구현 범위: 요청 라인/헤더/본문의 경계 식별, Content-Length 기반 본문 길이 처리, `Connection: keep-alive`의 의미와 처리
- 동시성 모델 선택의 트레이드오프: process-per-request vs thread pool vs event loop, C10K 관련 설계 고려사항
- Proxy vs Reverse Proxy의 차이: 누구의 입장에서 누구를 대신하는가, 보안 / 로드 밸런싱 / 캐싱 측면의 활용
- 메모리 관리: malloc/free 짝 맞추기, 버퍼 오버플로 방지, 클라이언트 끊김·비정상 종료 시 자원 회수
- 시그널 / 종료 처리: SIGINT / SIGTERM 안전한 셧다운, SIGPIPE 무시(쓰기 도중 클라이언트 끊김 대응)

## 품질
- 단위 테스트로 각 모듈(HTTP 파서, MIME 매핑, 라우팅 / 프록시 전달 로직 등)을 검증한다.
- 기능 테스트: curl, 브라우저 등 실제 HTTP 클라이언트로 정적 파일 응답 / 프록시 동작 확인, 동시 다중 클라이언트 접속 시 정상 동작 확인
- 부하 테스트: ab(Apache Bench), wrk, hey 등으로 동시 요청 수에 따른 처리량 측정
- 메모리 누수 검사: valgrind 또는 컴파일러 sanitizer(`-fsanitize=address,leak`)
- 엣지 케이스
  - 잘못된 HTTP 요청(불완전한 헤더, 비정상 메서드, 잘못된 버전)
  - 매우 큰 요청 본문 / 매우 긴 URI
  - 백엔드 서버 응답 없음 / 타임아웃(Reverse Proxy)
  - 클라이언트의 갑작스러운 연결 종료
- 이력서와 포트폴리오에 포함할 수 있을 만큼 완성도 높은 수준으로 구현한다.

## AI 함께하기 / 발표
- AI가 작성한 코드를 완벽히 내 것으로 소화해 그 원리를 설명할 수 있어야 한다.
- 주간 결과 발표(목요일 오전 10시): 데모와 설명. 반드시 테스트 케이스를 작성하고 검증 과정을 수행한 뒤 그 내용을 발표에 포함한다.
- 형식: 발표자료 없이 팀 GitHub 레포의 `README.md` 기준, 발표 4분 + QnA 1분.
- 제출물: 팀 GitHub Projects 주소, 팀 GitHub 레포 주소, WIL(블로그, `WEEK13` 태그).
