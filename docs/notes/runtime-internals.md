# 런타임 내부 동작 메모

[DESIGN.md](../DESIGN.md)에서 옮겨 온 개념 설명입니다. 설계 결정 자체는 DESIGN.md §3에, 여기는 그 결정을 이해하기 위한 배경만 둡니다.

## 파일 I/O는 논블로킹인가 — "네트워크는 논블로킹, 파일은 청크 동기"

소켓도 파일 디스크립터이므로 `O_NONBLOCK`을 걸면 `recv()`가 데이터 없을 때
`EAGAIN`을 돌려줍니다. 우리 서버의 네트워크 I/O는 이 방식입니다.

반면 일반 파일(디스크)은 다릅니다. `O_NONBLOCK`을 걸어도 대부분의 POSIX 구현은
정규 파일을 항상 "준비됨"으로 보고, `poll()`이 바로 리턴합니다. 실제 디스크
읽기는 페이지 캐시에서 빨리 끝나지만 캐시 미스에서는 여전히 블록할 수 있습니다.
진짜 비동기 파일 I/O는 POSIX AIO, Linux `io_uring`, Windows IOCP 같은 별도
기구가 필요합니다. 이 프로젝트는 C11/POSIX 외부 라이브러리 금지와 이식성 때문에
`static_handler`의 파일 읽기를 작은 청크로 나눠 동기 호출하고, 소켓 쓰기만
논블로킹으로 처리합니다. 즉 "네트워크 I/O는 non-blocking, 파일 I/O는 청크 동기"입니다.

## 데이터 흐름 (Data Flow)

```text
[Network]
   -> iomux (epoll/kqueue/poll)
   -> worker (accept 1건/이벤트)
   -> connection (feed: 비동기 recv + 버퍼)
   -> http_parser (메모리 버퍼만 읽음, FD 모름)
   -> route: static_handler | flight_api | cdn | proxy
   -> http_response (직렬화, 파일은 청크 스트림)
   -> connection (flush: 비동기 send, EAGAIN이면 다시 큐)
   -> [Network]
```

## 요청 처리 상세 (as-built)

코드가 지금 어떻게 도는지 설명하는 절입니다. 제약에 맞지 않거나 코드가 바뀌면 여기를 먼저 고칩니다.

### 요청 처리 흐름 (`connection.c`)

1. `recv` → `connection_feed()`：`http_parser`로 요청 경계 확정(`consumed`).
2. `config_match_route()`로 프록시 라우트를 먼저 확인.
   - 매칭되면 `proxy_handle()`가 백엔드로 전달하고 직접 소켓에 응답 후 연결 종료.
3. 내장 엔드포인트 `/health`, `/echo` 확인.
4. `/api/...`이면 `flight_api_build()` → `flight_store` 조회/변경.
5. 그 외는 `static_handler_resolve()` → 경로 보안(`realpath`) → CDN 조회 → 파일/캐시 응답.
6. 접근 로그: `[wN] [client_ip] "METHOD URI" status bytes ms`.

### 미니 CDN(로컬 TTL 캐시)을 둔 이유

`src/cdn.c`는 정적 자산 응답에 TTL·hit/miss·무효화·`X-Cache` 헤더를 적용하는 서버 내부 캐시다. 실제 엣지 네트워크가 아니라, 캐시 정책과 헤더 동작을 로컬 실행으로 검증하기 위한 계층이다.

### 현행 구조에서 잘 붙은 부분

- 단일 프로세스 + 워커 N개, 각자 iomux 인스턴스 소유(`epoll`/`kqueue`/`poll`) → fd 핸드오프 큐 없음.
- 이벤트당 accept 1건 덕분에 100동시 요청에서 좌석 경쟁이 실제로 재현됨.
- before(의도적 레이스)/after(뮤텍스) 분리, UI에는 데모 컨트롤 없음.
- 영속 저장은 임시 파일 → `rename` 원자적 교체.
- 구조 관련 확정 사항과 남은 작업은 [agent/DAILY_SCOPE.md](../agent/DAILY_SCOPE.md)의 "아키텍처 완료·남은 일" 절을 본다.

### 실제 동작 원리 (커널·소켓)

HTTP 스펙이 아니라 이 서버에서 실제로 일어나는 일을 적는다.

1. **TCP 소켓은 IP+포트만으로 식별되지 않는다** — 실제로는 `<src IP, src port, dst IP, dst port>` 4튜플로 구분한다. 그래서 같은 서버 `IP:포트`를 쓰는 여러 `conn fd`를 커널이 구분할 수 있다. (`src/tcpserver.c`, `src/worker.c`)
2. **커널 수신은 2단계** — Wait for Data(도착까지 기다림) → Copy Data(커널 수신 버퍼 → 유저 버퍼로 복사).
3. **`listen fd`와 `conn fd`는 다르다** — `listen fd`는 연결 요청을 받는 지점, `conn fd`는 성립된 연결 위의 데이터 경로.
4. **레벨 트리거(LT) / 엣지 트리거(ET)** — LT는 "읽을 데이터가 있다" 상태가 유지되는 동안 계속 통지하고, ET는 0에서 양수로 변하는 시점만 통지한다. 우리는 `EPOLLET` 없이 LT를 쓴다.
5. **accept 분산** — 워커가 `listen fd`를 공유하고 `iomux_wait`가 깨우면 한 번만 `accept()`하고, 나머지는 `EAGAIN`으로 스킵한다(D10).
6. **`dup()`로 소유권 이전** — 프록시 전용 스레드가 연결을 처리할 때 `dup()`으로 소켓을 복제해 워커 루프가 백엔드를 기다리지 않게 한다.
7. **유휴 연결 정리** — 마지막 활동 후 5초가 넘으면 닫는다. keep-alive 최대 100회도 여기서 확인한다.


## DESIGN.md에서 옮긴 기타 설명

### 외부 결제 서비스

결제는 `tests/backends/serve_payments.py`로 별도 프로세스(8500)에서 동작합니다.
게이트웨이는 `/api/payments/`를 `/`로 rewrite해 전달하고, 결제 서비스는 자기
파일 DB를 가집니다. 중복 방지 키는 요청 본문의 `idempotency_key`를 사용합니다.
프록시가 임의 헤더를 전달하지 않기 때문입니다. 재시작 후에도 상태가 남습니다.

### 의존 (Dependency)

A가 B의 헤더/타입/함수를 필요로 하면 A는 B에 의존합니다.
문제는 순환 의존입니다. `A.h`가 `B.h`를, `B.h`가 다시 `A.h`를 include하면
전방 선언과 구조체 레이아웃이 꼬입니다. 위 계층 규칙으로 이를 막습니다.

### 공용 헤더를 두지 않은 이유

`common.h` / `common.c`는 처음에만 잡아둔 빈 자리였고 실제 공유 코드가 없어 제거했습니다.
억지로 공용 헤더를 두면 모듈 간 결합만 생기므로, 공유가 실제로 필요해진 시점에 다시 둡니다.

### 소유권·의존·계약을 적는 이유

이 세 가지(소유권·의존·계약)가 문서화돼야 "파일을 나눴다"가 아니라
"모듈이 분리됐다"고 말할 수 있습니다.
