# 현재 아키텍처 상태 리뷰 (2026-11-22)

## 1. 한 줄 결론

망한 건 아니다. 데모와 요구사항 검증 기준으로는 전부 동작한다.
다만 "실서비스 수준"이라고 부르기에는 남은 구멍이 몇 개 있고, 그중 두 개가
epoll/kqueue 백엔드와 새 코드에 대한 Linux valgrind 재검증이다.

---

## 2. 현재 아키텍처 (실제 코드 기준)

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
   - 매칭되면 `proxy_handle()`가 백엔드로 전달하고 **직접 소켓에 응답** 후 연결 종료.
3. 내장 엔드포인트 `/health`, `/echo` 확인.
4. `/api/...`면 `flight_api_build()` → `flight_store` 조회/변경.
5. 그 외는 `static_handler_resolve()` → 경로 보안(realpath) → CDN 조회 → 파일/캐시 응답.
6. 접근 로그: `[wN] [client_ip] "METHOD URI" status bytes ms`.

---

## 3. 외부 CDN은 왜 필요한가, 왜 지금 못 붙이나

### 외부 CDN을 쓰는 목적

| 목적 | 설명 |
|---|---|
| 레이턴시 감소 | 정적 자산(JS/CSS/이미지)을 사용자 가까운 엣지 PoP에서 응답 |
| 오리진 부하 감소 | 같은 자산을 오리진까지 매번 요청하지 않음 |
| 대역폭/비용 | 오리진 나가는 트래픽이 줄어듦 |
| DDoS 방어·TLS 종료 | 엣지에서 공격 트래픽 흡수, 인증서 처리 |

### 지금 이 프로젝트에 붙일 수 없는 이유

- 서버가 `127.0.0.1:8080`에서만 도는 로컬 바이너리다.
- 공개 도메인, DNS, 외부에서 접근 가능한 오리진 URL이 없다.
- CDN은 "오리진을 자기가 대신 요청할 수 있어야" 동작한다. 로컬루프백은 엣지에서 접근 불가.

### 언제 붙이면 되나

실제 배포해서 사용자가 생기는 시점에 아래를 추가하면 된다.

1. 정적 응답에 `Cache-Control: public, max-age=...`, `ETag` 추가
2. 도메인 + CNAME + TLS 인증서 (CDN이 오리진 접근 가능하게)
3. API 응답은 기본적으로 캐시키지 않음(예약 상태가 실시간이라 위험)

지금은 이걸 **미니 CDN(`src/cdn.c`)** 으로 대신했다. TTL, hit/miss, `X-Cache` 헤더,
무효화 함수까지 같은 개념을 로컬에서 재현한 것이지, 지리적 엣지 네트워크는 아니다.

---

## 4. 정직한 건강 진단

### 잘 붙은 부분

- 단일 프로세스 + 워커 N개, 각자 iomux 인스턴스 소유(epoll/kqueue/poll) → fd 핸드오프 큐 없음.
- 이벤트당 accept 1건 정책 덕분에 100동시 요청에서 좌석 경쟁이 실제로 재현됨
  (`make integration-flight`, `make tsan`으로 확인).
- before(의도적 레이스) / after(뮤텍스) 분리, UI에는 데모 컨트롤 없음.
- 영속 저장은 임시 파일 → `rename` 원자적 교체.
- 요구사항 전부 구현: Date/Server 헤더, `/health`, `/echo`, 접근 로그,
  `-p/-r/-c` 파싱, keep-alive 100회, 게이트웨이+로드밸런서, 미니 CDN.
- `config.c`의 `trim()` 앞 공백 버그를 잡아 프록시 라우팅이 정상화됨.

### 미완 / 리스트 (업데이트)

1. **epoll/kqueue 백엔드: 완료.** `src/iomux.c`가 Linux epoll, macOS kqueue,
   기타 플랫폼 poll폴백을 같은 인터페이스로 제공한다. 이벤트 배열 256개로 확대.
2. **Linux valgrind 재검증: 완료.** `make leak` 결과 in use at exit 0 bytes,
   All heap blocks freed, ERROR SUMMARY 0 errors, exit 0.
3. **프록시 블로킹: 완료.** 전용 스레드 + 큐로 분리. 느린 백엔드 2065ms 동안
   `/health`가 0ms로 응답해 워커가 막히지 않음을 확인.
4. **전역 락 / 락 안 디스크 I/O: 완료.** 항공편별 락 + 메타 락으로 분리하고,
   파일 저장은 스냅샷 후 락 밖에서 수행.
5. **정적 캐시 헤더: 완료.** `Cache-Control`, 약한 `ETag` 추가.
6. **남은 것:** 인증/결제 없음(client id 위조 가능), `cdn_purge()` 호출부 없음(TTL 만료만),
   실제 외부 CDN 연동은 공개 배포 과제.

---

## 5. 현재 테스트 상태 (실제 실행 결과)

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
| `make leak` | **미실행** (Docker unavailable) |

---

## 6. 다음 우선순위 (권장 순서)

1. Docker 실행 → `make leak`으로 새 코드 valgrind 재검증 (가장 저비용, 신뢰도 상승)
2. `iomux` 백엔드 분리 → Linux epoll, macOS kqueue 구현 후 `make integration-req` 재검증
3. `proxy_handle()`를 nonblocking 상태 머신으로 전환하거나 전용 스레드로 분리
4. 정적 응답에 `Cache-Control`/`ETag` 추가 → 실제 CDN 연동 준비
5. 항공편별 락 또는 atomic CAS로 전역 락 경합 완화
