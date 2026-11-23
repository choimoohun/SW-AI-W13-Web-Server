# 이벤트 루프가 돌아가는 순서

우리 서버는 워커 스레드 N개(기본 4)가 각자 `iomux`(epoll/kqueue/poll) 루프를 돌리는 구조입니다.

## 용어부터

- 멀티플렉싱: 한 스레드가 여러 fd를 동시에 감시하는 방식. `select`/`poll`/`epoll`이 있음.
- `poll`: 관심 fd 목록을 매번 전부 스캔. fd가 많으면 O(n).
- `epoll`: 리눅스에서 준비된 fd만 돌려줌. fd가 많아도 준비된 것만 받으므로 O(준비 수).
- 이벤트 루프: `wait → 준비된 fd 처리 → 다시 wait`를 반복.

## 구조

```text
client A ──TCP──┐
client B ──TCP──┼──> listen fd (워커 4개가 공유)
client C ──TCP──┘
                     │
        ┌────────────┴─────────────┐
        │ worker0 epoll_wait()      │   worker1/2/3 같은 구조
        └────────────┬─────────────┘
                     │ 준비된 fd 목록
   ┌─────────────────┼──────────────────┐
   │ listen fd 준비   │ conn fd 준비      │
   │ accept_ready()  │ READ / WRITE      │
   └─────────────────┴──────────────────┘
```

## 워커 한 바퀴

`src/worker.c` 기준으로 이렇게 돕니다.

```text
while (실행 중) {
    n = iomux_wait(mux, events, MAX_EVENTS, timeout)   // 준비된 fd 대기
    for (준비된 이벤트) {
        if (listen fd) accept_ready()                  // 연결 1건 accept
        if (READ)      on_readable()                   // recv → 파서
        if (WRITE)     on_writable()                   // send 남은 것
    }
    cleanup_idle()                                     // 유휴 5초 정리
}
```

- `on_readable()`: `recv` → `connection_feed()` → `http_parser_parse()`.
  - `COMPLETE`면 응답을 `send_buf`에 넣고 `iomux_mod(WRITE)`로 쓰기 관심 전환.
  - `NEED_MORE`면 다음 `READ`를 기다림.
- `on_writable()`: `send()`로 남은 응답을 보냄. 다 보내면 연결 상태를 정리.
- `cleanup_idle()`: 마지막 활동 후 유휴 시간이 넘은 연결은 닫음. keep-alive 최대 100회도 여기서 확인.

## 타임라인 예시

느린 클라이언트 A와 빠른 클라이언트 B가 같은 워커에 있다고 가정.

```text
worker0 시간축
 | accept A | read A(일부) |      | read B | parse B | write B |      | read A(나머지) | parse A | write A |
           A는 천천히 보냄      B는 완료
```

A가 본문을 다 보내지 않아도 워커는 A를 기다리지 않고 B를 처리합니다. 이게 “블로킹하지 않는” 부분입니다.

## 왜 이 조합인가

- 스레드를 조금 두고(4개) 여러 연결을 감시 → 스레드 생성/컨텍스트 스위치 비용을 줄임.
- `epoll`은 준비된 fd만 돌려주므로, 연결이 수백 개여도 매번 전체를 훑지 않음.
- 느린 연결은 `READ`가 늦게 준비되는 것뿐이고, 다른 연결 처리를 막지 않음.
- 유휴 연결은 시간으로 정리해서 fd를 점유하지 않음.

## 프록시 요청은 별도

`/api/...`로 들어온 요청은 워커가 바로 백엔드와 통신하지 않습니다.

```text
worker: 프록시 요청 → 큐에 넣고 즉시 다음 이벤트 처리
proxy thread: 큐에서 꺼냄 → dup(client fd) → 백엔드 연결 → 중계
```

그래서 백엔드가 느려도 워커 루프는 멈추지 않습니다.

## 코드 위치

- `src/worker.c`: `worker_loop()`, `accept_ready()`, `on_readable()`, `on_writable()`, `cleanup_idle()`
- `src/iomux.c`: `iomux_create/add/mod/del/wait()` (epoll/kqueue/poll)
- `src/connection.c`: `connection_feed()`, `connection_flush()`
- `src/proxy.c`: `proxy_thread_main()`, 작업 큐
