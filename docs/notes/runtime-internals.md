# 런타임 동작과 네트워크 개념

이 서버가 실제로 어떻게 도는지(as-built)와, 그 설계를 이해하는 데 필요한 네트워크 개념을 모은다.
설계 결정은 [DESIGN.md](../DESIGN.md) §3에, 멀티플렉싱 선택 근거는 [iomux.md](iomux.md)에 있다.

## 1. 데이터 흐름

```text
[Network]
   -> iomux (epoll/kqueue/poll)
   -> worker (accept 1건/이벤트)
   -> connection (feed: 비동기 recv + 버퍼)
   -> http_parser (메모리 버퍼만 읽음, fd 모름)
   -> route: static_handler | flight_api | cdn | proxy
   -> http_response (직렬화, 파일은 청크 스트림)
   -> connection (flush: 비동기 send, EAGAIN이면 다시 대기)
   -> [Network]
```

## 2. 요청 처리 절차 (`connection.c`)

1. `recv` → `connection_feed()` → `http_parser_parse()`로 요청 경계 확정(`consumed`).
2. `config_match_route()`로 프록시 라우트를 먼저 확인. 매칭되면 `proxy_submit()`으로 소유권 이전.
3. `/health`, `/echo` 같은 내장 엔드포인트 확인.
4. `/api/...`이면 `flight_api_build()` → `flight_store` 조회·변경.
5. 나머지는 `static_handler_resolve()` → `realpath` 경계 검사 → CDN 조회 → 파일/캐시 응답.
6. 접근 로그 `[wN] [client_ip] "METHOD URI" status bytes ms`.

코드가 바뀌면 이 절을 먼저 고친다.

## 3. 소켓과 식별

- **소켓**은 `<protocol, IP, port>`로 정의하고, **커넥션**은 `<src IP, src port, dst IP, dst port>` 4튜플로 식별한다(RFC 793).
- 같은 서버 `IP:port`를 쓰는 여러 `conn fd`를 커널이 구분할 수 있다. 우리가 이 식별을 직접 하지는 않고, 클라이언트 IP만 `getpeername()`으로 읽어 접근 로그에 쓴다.
- **`listen fd`**는 연결 요청을 받는 창구, **`conn fd`**는 성립된 연결 위의 데이터 경로.
- 커널 수신은 2단계다. Wait for Data(도착 대기) → Copy Data(커널 버퍼 → 사용자 버퍼).

## 4. 동시성과 병렬성

- **동시성(concurrency)**: 여러 작업이 겹치는 시간대에 진행. 번갈아 실행(인터리빙)도 포함이라 코어 1개로도 성립.
- **병렬성(parallelism)**: 같은 순간에 실제로 같이 실행. 물리적 코어 필요.
- 이 서버는 둘을 섞는다. 동시성은 `iomux` 이벤트 루프에서, 병렬성은 워커 스레드 4개가 코어 4개에서 도는 부분에서 나온다.
- CPU 바운드는 스레드 수로 늘리고, I/O 대기는 이벤트 루프로 겹친다.

## 5. 실행 주체: 스레드 + 이벤트 루프

- 단일 프로세스 + 워커 스레드 N개(D5). 좌석 상태를 뮤텍스로 공유.
- `fork` 멀티프로세스는 프로세스마다 좌석이 따로 놀아 공유 메모리/IPC가 필요하고, accept 분산·좀비 처리·재기동이 복잡해진다. 대가로 이 구조엔 프로세스 격리가 없다.
- 워커마다 `iomux` 인스턴스를 소유하므로 fd 핸드오프 큐가 필요 없다. 이벤트당 `accept()`는 1건(D10).

## 6. 느린 백엔드 격리

- 워커가 백엔드 응답을 기다리면 그 워커는 다른 요청을 못 처리한다.
- 프록시 요청은 워커가 작업을 큐에 넣고 즉시 반환하고, 전용 스레드 `proxy_thread_main`이 꺼내 통신한다. `dup()`으로 client fd 소유권을 넘긴다.
- 효과: 느린 백엔드가 2초 걸려도 `/health`는 영향이 없다(`make integration-req`에서 확인).

## 7. 리버스 프록시와 로드밸런싱

- 클라이언트는 한 포트(8085)만 본다. `conf/backends.conf`의 `route = prefix|backend...`가 경로와 백엔드 매핑을 담는다.
- 로드밸런싱은 **같은 기능**을 하는 복제본이 둘 이상일 때만 성립한다(`/api/orders/` → 8401, 8402 Round-Robin). 기능이 다른 백엔드(cache·stocks·payments)는 라우팅만 한다.
- 포워드 프록시는 반대 방향(클라이언트 대신 외부로)이며 미구현(범위 밖).

## 8. HTTP 메시지 경계와 응답 확인

- TCP는 스트림이라 메시지 경계가 없다. `\r\n\r\n`으로 헤더 끝을, `Content-Length`로 본문 끝을 정한다. short read/write는 상태 머신(`connection_feed`/`connection_flush`)으로 처리한다.
- 응답 확인 대상은 헤더와 본문 둘 다: 상태 줄, `Content-Type`, `Content-Length`, `Connection`, 본문 길이와 바이너리 무결성.

## 9. HTTP/2 ‘멀티플렉싱’ vs I/O 멀티플렉싱

이름이 같을 뿐 다른 장치다.

| | HTTP/2 스트림 멀티플렉싱 | I/O 멀티플렉싱(`epoll`) |
|---|---|---|
| 무엇을 섞나 | 하나의 TCP 연결 위 여러 요청·응답 | 한 스레드가 감시하는 여러 fd |
| 구분 수단 | 프레임의 `stream id` | fd 번호와 커널의 준비 목록 |
| 누가 구분하나 | 수신쪽이 프레임을 재조립 | 커널이 준비된 fd만 알려줌 |

“멀티플렉싱을 하면 HTTP/2가 되는가”는 성립하지 않는다. h2는 앞단 게이트웨이에서 종료한다(D26).

## 10. 순서 보장과 기타

- TCP는 바이트 순서를 항상 보장한다. HTTP/1.1은 한 연결에서 순서 처리라 앞 요청이 느리면 뒤가 막힌다(HOL). HTTP/3(QUIC)가 TCP HOL을 제거한다.
- TLS는 비대칭키로 대칭키를 합의한 뒤 데이터는 대칭키로 암호화한다. 우리는 TLS를 구현하지 않고 앞단 게이트웨이에서 종료한다(D26).
- 대역폭은 초당 전송량, 지연은 첫 바이트까지 시간. 이 시연의 병목은 대역폭이 아니라 느린 백엔드·느린 클라이언트.

## 11. 미니 CDN

`src/cdn.c`는 정적 자산 응답에 TTL·hit/miss·`X-Cache` 헤더를 적용하는 서버 내부 캐시다. 실제 엣지 네트워크가 아니라 캐시 정책과 헤더 동작을 로컬에서 검증하는 계층이다.

## 12. 외부 결제 서비스

결제는 `tests/backends/serve_payments.py`(별도 프로세스 8500)에서 동작한다. 게이트웨이는 `/api/payments/`를 `/`로 rewrite하고, 결제 서비스는 자기 파일 DB를 가진다. 중복 방지 키는 본문의 `idempotency_key`를 쓰는데, 프록시가 임의 헤더를 넘기지 않기 때문이다([D28](../decisions/STEP06_FLIGHT_DEMO_DECISIONS.md)).

## 13. 모듈 분리와 확장 여지

지금 구조는 모듈 경계가 명확해야 나중에 일부만 바꿀 수 있다.

- `worker`/`iomux`: 워커 수를 바꾸거나 관심 집합을 교체할 수 있다.
- `proxy`: 큐 처리 방식(스레드 수, 비동기화)과 백엔드 풀을 독립적으로 바꿀 수 있다.
- `connection`: feed/flush 상태 머신 위에 keep-alive 외에 새 프레이밍(예: WebSocket)을 얹을 수 있다.
- `cdn`/`static_handler`: 캐시 정책과 파일 경로 검사를 따로 교체할 수 있다.

다만 지금은 시연 규모에 맞춰 의존을 단순하게 유지한다. 나눌지 여부는 [../agent/DAILY_SCOPE.md](../agent/DAILY_SCOPE.md)의 “아키텍처 완료·남은 일” 절을 본다.
