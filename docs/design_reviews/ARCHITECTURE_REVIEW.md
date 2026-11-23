# 현재 아키텍처 상태 리뷰 (2026-11-22)

## 한 줄 결론

망한 건 아니다. 데모와 요구사항 검증 기준으로는 전부 동작한다.
다만 "실서비스 수준"이라고 부르기에는 남은 구멍이 몇 개 있고, 그중 두 개가
epoll/kqueue 백엔드와 새 코드에 대한 Linux valgrind 재검증이다.

---

## 현재 아키텍처 (실제 코드 기준)

```text
                         브라우저 (flights.html / ops.html)
                         HTTP/1.1 keep-alive (최대 100회)
                                  |
                                  v
              tcpserver_listen()  단일 listen fd (nonblocking)
                                  |
        워커 4개가 같은 listen fd에서 accept 경쟁 (이벤트당 1건만)
                                  |
        +-------------------------+-------------------------+
        |                         |                         |
     worker 0                 worker 1                  worker 3
   (poll 인스턴스)           (poll 인스턴스)            (poll 인스턴스)
        |                         |                         |
        +----------- connection_feed / connection_flush ----+
                    (READ/WRITE 이벤트 상태 머신)
                                  |
                  +---------------+----------------+
                  |                                |
        config_match_route()               워커 0에서만 200ms 간격
        prefix 매칭                         flight_store_tick()
                  |                        (자동 출발/도착)
        +---------+----------+
        |                    |
    매칭됨               매칭 안 됨
        |                    |
   proxy_handle()      +------+-------------------+
   게이트웨이+RR       |      |                   |
   XFF, 502/504    /health  /api/...        정적 파일
                   /echo      |                |
                            flight_api      static_handler
                                |                |
                        flight_store         cdn (TTL 캐시)
                        전역 pthread_mutex    X-Cache HIT/MISS
                        파일 영속 저장        16KB 미만은 메모리 응답
                        data/flights.txt     나머지는 청크 스트리밍
                        (임시파일→rename)
```

### 요청 처리 흐름 (connection.c)

1. `recv` → `connection_feed()`：`http_parser`로 요청 경계 확정(`consumed`).
2. `config_match_route()`로 프록시 라우트 먼저 확인.
   - 매칭되면 `proxy_handle()`가 백엔드로 전달하고 직접 소켓에 응답 후 연결 종료.
3. 내장 엔드포인트 `/health`, `/echo` 확인.
4. `/api/...`면 `flight_api_build()` → `flight_store` 조회/변경.
5. 그 외는 `static_handler_resolve()` → 경로 보안(realpath) → CDN 조회 → 파일/캐시 응답.
6. 접근 로그: `[wN] [client_ip] "METHOD URI" status bytes ms`.

---

## 미니 CDN(로컬 TTL 캐시)을 둔 이유

`src/cdn.c`는 정적 자산 응답에 TTL·hit/miss·무효화·`X-Cache` 헤더를 적용하는
서버 내부 캐시다. 실제 엣지 네트워크가 아니라, 캐시 정책과 헤더 동작을
로컬 실행으로 검증하기 위한 계층이다.

---

## 정직한 건강 진단

### 잘 붙은 부분

- 단일 프로세스 + 워커 N개, 각자 iomux 인스턴스 소유(epoll/kqueue/poll) → fd 핸드오프 큐 없음.
- 이벤트당 accept 1건 정책 덕분에 100동시 요청에서 좌석 경쟁이 실제로 재현됨
  (`make integration-flight`, `make tsan`으로 확인).
- before(의도적 레이스) / after(뮤텍스) 분리, UI에는 데모 컨트롤 없음.
- 영속 저장은 임시 파일 → `rename` 원자적 교체.
- 요구사항 전부 구현: Date/Server 헤더, `/health`, `/echo`, 접근 로그,
  `-p/-r/-c` 파싱, keep-alive 100회, 게이트웨이+로드밸런서, 미니 CDN.
- `config.c`의 `trim()` 앞 공백 버그를 잡아 프록시 라우팅이 정상화됨.

### 완료 항목 / 남은 것

1. epoll/kqueue 백엔드: 완료. `src/iomux.c`가 Linux epoll, macOS kqueue,
   기타 플랫폼 poll폴백을 같은 인터페이스로 제공한다. 이벤트 배열 256개로 확대.
2. Linux valgrind 재검증: 완료. `make leak` 결과 in use at exit 0 bytes,
   All heap blocks freed, ERROR SUMMARY 0 errors, exit 0.
3. 프록시 블로킹: 완료. 전용 스레드 + 큐로 분리. 느린 백엔드 2065ms 동안
   `/health`가 0ms로 응답해 워커가 막히지 않음을 확인.
4. 전역 락 / 락 안 디스크 I/O: 완료. 항공편별 락 + 메타 락으로 분리하고,
   파일 저장은 스냅샷 후 락 밖에서 수행.
5. 정적 캐시 헤더: 완료. `Cache-Control`, 약한 `ETag` 추가.
6. 남은 것: 인증 없음(client id 위조 가능), `cdn_purge()` API는 있지만
   호출부가 없어 캐시는 TTL 만료로만 정리됨. `/api/cache/miss`는 프록시 502
   검증용 경로이며 캐시 무효화 엔드포인트가 아님.

---

## 현재 테스트 상태 (실제 실행 결과)

| 명령 | 결과 |
|---|---|
| `make` | `-Wall -Wextra -Werror` 통과 |
| `make test` | parser, static PASS |
| `make test-flight` | after 1/32(잔여 0), before 32/32(잔여 -31) PASS |
| `make integration` | 404/403/405/바이너리/MIME PASS |
| `make integration-flight` | 예약/취소/keep-alive/slow client/before·after PASS |
| `make integration-persist` | 재시작 후 예약·좌석·이력 유지 6항목 PASS |
| `make test-tracking` / `integration-tracking` | 시계 전이·지연·자동 출발/도착 PASS |
| `make integration-req` | 헤더/CDN/health/echo/프록시 RR/로그 전부 PASS |
| `make load-test` | 200요청·동시 20, 성공 200, Non-2xx 0, 약 568 RPS |
| `make leak` | in use at exit 0 bytes, ERROR SUMMARY 0 errors, exit 0 |

---

## 완료한 일

아래 1~5번은 완료했다.

1. `make leak` valgrind 재검증 — 완료. in use 0 bytes, ERROR SUMMARY 0.
2. `iomux` 백엔드 분리 — 완료. Linux epoll / macOS kqueue / poll 폴백,
   `tests/linux_check.sh`로 재검증.
3. `proxy_handle()` 블로킹 — 완료. 전용 스레드 + 큐로 분리,
   느린 백엔드 2065ms 동안 `/health` 0ms 확인.
4. 정적 캐시 헤더 — 완료. `Cache-Control`, 약한 `ETag` 추가.
   목적은 외부 연동이 아니라 로컬 캐시 정책 검증임.
5. 전역 락 경합 — 완료. 항공편별 락 + 메타 락, 파일 저장은 스냅샷 후 락 밖에서 수행.

남은 일:

1. `flight_api.c` / `proxy.c`의 책임 단위 분리(JSON·라우터·핸들러, 라우터·백엔드풀·스레드).
2. 정적 파일 I/O의 진짜 비동기화(Linux `io_uring`, POSIX AIO) — 이식성과 트레이드오프 검토 필요.
3. `cdn_purge()` 호출 경로 연결(무효화 API) 또는 명시적으로 TTL-only로 확정.
4. 인증 도입 시 client id 위조 문제 해결.
5. 선택 실험: `SO_REUSEPORT` / `EPOLLEXCLUSIVE`로 accept 단계의 wake-up 비용 측정.
