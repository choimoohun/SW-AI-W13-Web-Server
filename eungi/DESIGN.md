# DESIGN.md — C 웹서버 & 프록시 설계

외부 라이브러리 없이 C와 POSIX API만으로 웹서버와 프록시를 구현하기 위한 설계 문서다.
모듈을 어떻게 나눴는지, 요청이 들어와 응답이 나가기까지 어떤 순서로 처리되는지, 각 모듈이 무엇을 하는지 정리한다.

> 구현 범위: 이 문서는 프록시·CGI·이벤트 루프를 포함한 최종 설계를 설명한다.
> 현재 구현·검증 범위는 정적 파일 서빙까지다 (TCP 수락 → HTTP 파싱 → 정적 파일 → 응답 전송 → 연결 정리).
> §1의 poll 이벤트 루프·fork 워커, §4.5 router, §4.6 CGI/프록시, cache는 아직 구현하지 않았다.
> 동시성 모델(FR-06)은 확정 전이며 DECISIONS.md에 기록할 예정이다.
> 실제 트리는 server.* 대신 tcpserver.*, conf/server.conf 대신 conf/tcpserver.conf를 쓴다.
> 설계 검토 내용은 verify/DESIGN_REVIEW.md에 있다.

---

## 1. 동시성 모델과 이식성

동시 연결 처리는 poll 기반 이벤트 루프를 기본으로 한다.
소켓을 논블로킹으로 두고 poll이 읽기/쓰기 가능한 연결만 알려주면, 단일 스레드가 준비된 연결을 번갈아 처리한다. 연결당 스레드를 두는 방식보다 적은 자원으로 많은 연결을 감당할 수 있다.

여러 CPU를 쓰려면 워커 프로세스를 늘린다. 설정의 workers 값만큼 시작 시 fork해서 프로세스를 복제한다. 개발·디버깅 때는 workers=1로 둔다.

이벤트 루프는 OS마다 API가 다르다(리눅스 epoll, macOS kqueue). 양쪽에서 동일 소스로 빌드되어야 하므로(C-03) 우선 POSIX 표준 poll로 구현하고, 이벤트 루프를 reactor 모듈로 분리해 나중에 내부만 교체할 수 있게 한다.

모듈 분리는 공통 도구와 기능 구현을 나누는 기준을 따른다. 소켓 열기, 송수신, HTTP 파싱처럼 여러 기능이 함께 쓰는 것은 역할별 모듈로 두고, 정적 파일·CGI·프록시처럼 독립된 기능은 기능별 모듈로 떼어낸다.

지원 메서드는 GET, POST다. 본문 길이는 Content-Length로만 판단하고, Transfer-Encoding: chunked는 지원하지 않으며 501로 거절한다.

---

## 2. 요청 처리 흐름

브라우저 요청은 다음 순서로 처리된다.

```text
클라이언트(브라우저 / curl / wrk)
  → tcpserver      연결 수락 (master는 종료 신호 감시, worker는 이벤트 루프)
  → http_parser    요청 줄 → 헤더 → 본문 해석
  → router         경로에 따라 핸들러 선택
      ├─ static_handler   정적 파일
      ├─ cgi_handler      외부 프로그램 실행
      └─ proxy_handler    백엔드 중계
```

---

## 3. 디렉터리 구조

```text
.
├── Makefile                # 빌드 / 테스트 / 메모리 검사 타겟
├── conf/server.conf        # 포트, 워커 수, doc_root, 프록시 규칙
├── include/                # 모듈별 헤더
│   ├── common.h            # 공용 상수(버퍼 크기, 타임아웃 등)
│   ├── config.h            # 설정 파일 / CLI 파싱
│   ├── log.h               # 접근 로그
│   ├── reactor.h           # 이벤트 루프(poll)
│   ├── connection.h        # 연결별 상태 / 버퍼
│   ├── http_parser.h       # HTTP 요청 파싱
│   ├── http_response.h     # HTTP 응답 생성 / 전송
│   ├── router.h            # 경로 → 핸들러 분기
│   ├── static_handler.h    # 정적 파일
│   ├── cgi_handler.h       # CGI
│   ├── proxy_handler.h     # 프록시
│   └── cache.h             # (선택) 응답 캐시
├── src/                    # 위 헤더의 구현 + main.c, tcpserver.c
├── www/                    # 정적 파일 루트
├── cgi-bin/                # CGI 실행 파일
└── tests/                  # unit / integration / load
```

---

## 4. 모듈별 동작

### 4.1 config
설정 우선순위는 기본값 → 설정 파일(-c) → CLI(-p 등) 순이다. 설정 파일은 key=value 형식이다.

```ini
port = 8080
workers = 1
doc_root = ./www
cgi_dir = ./cgi-bin
enable_forward_proxy = 0
proxy_pass /api/ 127.0.0.1:8001,127.0.0.1:8002
```

### 4.2 tcpserver / reactor
listen 소켓은 getaddrinfo → socket → setsockopt(SO_REUSEADDR) → bind → listen 순으로 만든다. SO_REUSEADDR는 종료 직후 같은 포트 재바인딩을 허용한다. 소켓은 O_NONBLOCK으로 둔다.
reactor는 fd와 관심 이벤트(읽기/쓰기)를 등록하고, poll로 준비된 fd의 처리 함수를 호출한다.

### 4.3 connection
TCP는 바이트 스트림이라 요청이 여러 번에 나눠 도착하고, 응답도 한 번에 다 보내지 못할 수 있다. 연결마다 상태와 버퍼를 두고 어디까지 처리했는지 기록한다.
기본 버퍼(읽기/쓰기 각 16KB)는 연결 구조체에 포함하고, 큰 POST 본문만 임시 할당했다가 해제한다.
Keep-Alive 시 응답 후 버퍼에 남은 다음 요청 데이터는 memmove로 앞으로 당겨 이어서 처리한다.

### 4.4 http_parser / http_response
파서는 누적 버퍼에서 \r\n\r\n으로 헤더 끝을 찾는다. 헤더가 8KB를 넘으면 431(또는 400)로 거절한다.
요청 줄에서 GET/POST가 아니면 405, URI가 2048바이트를 넘으면 414. http://로 시작하면 포워드 프록시 요청으로 표시한다.
Host는 HTTP/1.1에서 필수며 없으면 400. Content-Length로 본문 길이를 판단하고 1MB 초과 시 413. Connection: close면 응답 후 닫는다.
응답은 상태 줄, 헤더, 빈 줄, 본문 순으로 직렬화해 전송한다.

### 4.5 router
파싱이 끝나면 순서대로 분기한다: 포워드 프록시(활성화 시) → 리버스 프록시(경로 일치, 최장 prefix 우선) → CGI(/cgi-bin/) → /health → 정적 파일.

### 4.6 핸들러
static_handler: query 제거 → 퍼센트 디코딩 → .. 차단 → doc_root 결합 → realpath로 경계 확인. 경로 탈출은 403. 없으면 404, 권한 없으면 403. 확장자로 Content-Type을 정한다. 큰 파일은 16KB씩 읽어 보낸다.
cgi_handler: fork + pipe 2개로 자식 프로세스의 stdin/stdout을 연결하고 execve로 실행한다. waitpid(WNOHANG)으로 좀비를 정리한다.
proxy_handler: 리버스 프록시는 백엔드를 Round-Robin으로 선택해 논블로킹 connect로 중계한다. X-Forwarded-For를 추가한다. 연결 실패는 502, 응답 타임아웃(5초)은 504. 포워드 프록시는 getaddrinfo로 원 서버에 접속해 중계하며, 선택적으로 캐시한다.

---

## 5. 안정성 규칙

1. 정리는 conn_close 한 곳에서만 한다. 여러 곳에서 free/close하면 이중 해제가 생기므로 fd 닫기와 메모리 해제를 단일 지점으로 모은다.
2. 시작 시 SIGPIPE를 무시한다(SIG_IGN). 전송 중 클라이언트가 끊어도 서버가 죽지 않는다.
3. SIGINT/SIGTERM을 받으면 새 연결 수락을 멈추고 열린 연결/메모리를 정리한 뒤 종료 코드 0으로 끝낸다.
4. 타임아웃 5초. 연결만 열고 데이터를 안 보내는 경우(Slowloris)나 응답 없는 백엔드를 정리한다.
5. 사용하지 않는 fd는 -1로 초기화하고 close 직후에도 -1로 둔다. 0도 유효한 fd이므로 잘못된 close를 막는다.

---

## 6. 개발 순서

한 번에 전부 구현하지 않고 단계별로 만들고 검증한다.

1. 뼈대: 실행, poll, 접속 시 Hello 응답, Ctrl+C 종료 시 누수 없음.
2. 파서 + 정적 파일 + ../ 차단: curl/make test로 200/403/404/POST/Keep-Alive 확인.
3. 프록시 & CGI: 가짜 백엔드 2대로 Round-Robin/502, CGI 동작 확인.
4. 검증: make asan/valgrind 누수 검사, wrk 부하 테스트, workers=2 결과를 README에 정리.
