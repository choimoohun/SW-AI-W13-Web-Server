# I/O 멀티플렉싱 — 왜 이 방식을 골랐는가

FR-06(동시 요청 처리)와 NFR-03(타임아웃·연결 수명)을 푸는 핵심 선택이다.
이 문서는 “왜 블로킹 `read`를 버렸는가”, “왜 `select`가 아니라 `epoll`/`kqueue`인가”, “왜 `poll`을 폴백으로 남겼는가”를 다룬다.

## 0. 설명용 전체 흐름

1. **연결이 어떻게 생기나.** 클라이언트가 리스닝 소켓으로 연결 요청을 하면 연결이 성립되고, 서버는 연결 전용 소켓을 하나 만든다. **리스닝 소켓은 하나인데 받는 연결은 여러 개**다. 리스닝 소켓은 모든 클라이언트가 같이 쓰는 지점이고, 연결 전용 소켓은 한 연결이 혼자 쓴다.
2. **순차 처리의 문제.** 한 스레드가 요청을 처음부터 끝까지 도맡으면, 다른 연결은 그 시간 동안 아무 일도 못한다.
3. **두 가지 해결 방향.**
   - 스레드를 늘려 코어별로 진짜 병렬 처리. 대가는 메모리와 컨텍스트 스위치.
   - 한 스레드가 여러 연결을 돌아가며 처리. 이것이 이벤트 루프.
4. **이벤트 루프에서 I/O를 기다리는 방법.**
   - 블로킹 I/O — 응답이 올 때까지 스레드가 잠들고 다른 연결이 막힌다.
   - 논블로킹 I/O — 안 오면 `EAGAIN`으로 즉시 반환. 다른 일을 할 수 있지만 계속 확인해야 CPU를 소모한다.
   - I/O 멀티플렉싱 — 관심 있는 여러 연결을 커널에 등록해 두고, 커널이 준비된 연결만 골라 알려준다.
5. **우리의 선택.** 스레드 4개로 CPU 일을 병렬화하고, 각 스레드가 멀티플렉싱으로 연결을 돌린다.

```text
클라이언트 ──connect──► listen fd ──accept──► 연결 전용 소켓
                                                      │
             ┌────────────────────────────────────────┘
             ▼
      워커(고정 4개)가 여러 소켓을 감시 등록
             │
             ├─ 데이터 없음 → 잠듦
             └─ 준비된 소켓 → 깨어나서 처리 → 다시 잠듦
```

`read()`는 커널 수신 버퍼에서 앱 버퍼로, `write()`는 그 반대로 복사한다. 앱은 NIC를 직접 다루지 않는다.

### 단계별로 보면

| 시간 | 무슨 일 | 워커 |
|---|---|---|
| t0 | 시작. 관심 집합 = `listen fd` | 잠듦 |
| t1 | A가 `connect()`. 커널이 listen fd 준비라고 알림 | 깨어남 → `accept()` 한 번 → conn A 생성 → 관심에 추가 |
| t2 | B가 `connect()` | 깨어남 → `accept()` 한 번 → conn B 추가 |
| t3 | A가 `GET /`를 본냄. 커널이 conn A READ 준비라고 알림 | 깨어남 → `recv()` → 파싱 → 응답을 버퍼에 만들고 WRITE 관심 추가 |
| t4 | 소켓 쓰기가 가능해짐 | 깨어남 → `send()` |
| t5 | 준비된 것 없음 | 그대로 잠듦(타임아웃 200ms) |

### 그림과 코드 대응

| 그림의 단계 | 코드 |
|---|---|
| 잠들기·깨어나기 | `iomux_wait(mux, events, MAX_EVENTS, 200)` — `src/worker.c` |
| `accept()` | `accept_ready()` |
| `recv`/파싱 | `on_readable()` → `connection_feed()` |
| `send()` | `on_writable()` → `connection_flush()` |
| 관심 추가·수정 | `iomux_add()`, `iomux_mod()` — `src/iomux.c` |

## 1. 웹서버의 성격이 결정했다

- 동시 연결이 많다. 브라우저는 도메인당 여섯 개 정도를 병렬로 열고 keep-alive로 같은 연결을 계속 쓴다.
- 요청 하나의 일은 짧다. 긴 계산을 하지 않는다.
- 대부분의 시간은 유휴다. 다음 요청을 보내기까지 소켓에 데이터가 없다.
- 느린 클라이언트가 있다. 데이터를 천천히 본내는 연결이 섞여 있다.

연결당 스레드·프로세스를 두면 유휴 시간에도 자원을 점유하고, 느린 클라이언트는 스레드를 오래 붙잡는다. 멀티플렉싱은 스레드 수를 고정하고 “지금 준비된 연결”만 처리하게 해서 이 구조를 뒤집는다. 유휴 연결은 커널 버퍼에만 있고 스레드를 차지하지 않는다.

## 2. 왜 `epoll`인가 (`kqueue`, `poll`)

- `select`/`poll`은 관심 fd 전체를 매 호출마다 넘기고 커널도 전체를 훑는다. 1000개 중 준비된 것이 몇 개뿐이어도 훑는 비용은 같다.
- `epoll`은 관심 집합을 커널에 등록해 두고 준비된 fd만 돌려받는다. 훑는 범위가 준비된 수에만 비례한다.

“동시 연결이 많고 활성은 적다”는 성격이 `epoll`을 고르는 이유다. `epoll`이 Linux 전용이라 macOS는 `kqueue`, 그 외는 POSIX `poll`을 폴백으로 쓴다.

| API | 관심 fd 전달 | 훑는 범위 | 이식성 |
|---|---|---|---|
| `select` | `fd_set` 3개를 매 호출 | 0..`fdmax` 전부, `FD_SETSIZE` 제한 | POSIX |
| `poll` | `struct pollfd` 배열을 매 호출 | 관심 fd 전체 | POSIX |
| `epoll` | `epoll_ctl()`로 커널에 등록 | 준비된 fd만 | Linux |
| `kqueue` | `kevent()`로 커널에 등록 | 준비된 fd만 | macOS/BSD |

반대 성격(긴 CPU 계산, 연결 수가 적고 단순함이 중요, 공유 상태가 많아 뮤텍스가 병목)에서는 블로킹 + 스레드가 더 단순하고 빠르다. 좌석 경쟁을 스레드 + 뮤텍스로 푼 것은 그런 이유에서다.

## 3. 레벨 트리거와 엣지 트리거

- 레벨 트리거(LT): “읽을 데이터가 있다” 상태가 유지되는 동안 계속 통지. 덜 읽어도 다음 호출에서 다시 알려준다.
- 엣지 트리거(ET): 데이터가 0에서 양수로 변하는 시점에만 통지. 그 반복에서 다 읽지 않으면 다음 통지를 기다려야 한다.

우리는 `EPOLLET`를 쓰지 않는다. 기본 LT다.

## 4. 왜 `epoll`/`kqueue` + `poll` 폴백인가

결정은 D5, D9, D21(`../decisions/STEP05_CONCURRENCY_DECISIONS.md`)이다. 확장성과 이식성을 같이 잡기 위해서다.

- `epoll`(Linux) / `kqueue`(macOS) 기본, `poll` 폴백.
- 셋을 `iomux` 인터페이스 뒤에 숨겨 워커 코드는 플랫폼과 무관하게 유지.
- 대가는 `#ifdef` 분기. 관심 fd가 적으면 `poll`만으로 충분하다.

## 5. 우리 코드에서 동작

```c
#if defined(__linux__)          -> epoll_create1 / epoll_ctl / epoll_wait
#elif defined(__APPLE__) ...    -> kqueue / kevent
#else                           -> poll
#endif
```

- `iomux_create()`: 워커마다 1개.
- `iomux_add/mod/del()`: 관심 fd 등록·변경·제거.
- `iomux_wait()`: 준비된 이벤트를 `out[]`에 최대 `max`개. 0이면 타임아웃.
- 등록 이벤트는 `EPOLLIN`/`EPOLLOUT` + `EPOLLRDHUP`/`EPOLLHUP`/`EPOLLERR`, `EPOLLET` 없음(LT).

`src/worker.c`:

```text
while (실행 중) {
    iomux_wait(mux, events, 256, 200ms)
    for (준비된 이벤트) { accept / on_readable / on_writable }
    cleanup_idle()                        // 유휴 5초 정리
}
```

## 6. 멀티플렉싱을 고르는 경우와 고르지 않는 경우

- 멀티플렉싱이 유리: 한 스레드가 여러 연결을 같이 다뤄야 할 때, 연결은 많지만 활성이 적을 때, 스레드·동기화 오버헤드를 피하고 싶을 때.
- 블로킹 스레드가 나음: 요청마다 CPU 작업이 길 때, 연결이 적고 단순함이 중요할 때, 공유 상태가 많아 뮤텍스가 병목일 때.

우리는 연결 처리는 멀티플렉싱, 공유 상태(좌석)는 스레드 + 뮤텍스로 섞었다.

## 7. 왜 멀티프로세스가 아니라 스레드인가

| 대안 | 문제 |
|---|---|
| 멀티프로세스 | `fork()`가 메모리를 복사해 좌석 배열이 워커마다 따로 생김 → 공유 메모리·IPC 필요 |
| 메시지 큐로 동기화 | 같은 프로그램 안에서 상태 공유만 하면 되는데 비용만 커짐 |
| fd 핸드오프 | 워커 사이에 소켓을 넘기는 구조가 필요. 우리는 accept한 워커가 연결을 끝까지 소유 |

멀티프로세스가 낫다는 근거(워커 장애 격리, 프로세스별 재기동)도 있지만 지금 규모엔 요구가 없어 미뤘다. 검토 기준은 `tests/integration/review_cases.sh` TC4.

## 8. 검증

- `make test`, `make integration`, `make edge-cases` — 수신 버퍼 동적 확장, 경로 탈출, Content-Length 검증, 큰 본문(8KB 정상 / 1MB+1 거부).
- macOS는 `kqueue`, Linux는 `epoll`이 선택되는지 `tests/linux_check.sh`로 확인.
- 동시 100×1000은 `make load-test-heavy`. 수치는 [../records/VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md) 기준.

## 9. 참고 자료

- Karunesh Johri, [I/O multiplexing: select, poll and epoll in Linux](https://www.softprayog.in/programming/io-multiplexing-select-poll-epoll-in-linux)
- [Chris Kanich — what's the difference between processes, threads, and io multiplexing?](https://www.youtube.com/watch?v=85T_ZaT8EUI)
- 관련 문서: [../DESIGN.md](../DESIGN.md)(루프 동작 순서·큐 위치), [../decisions/STEP05_CONCURRENCY_DECISIONS.md](../decisions/STEP05_CONCURRENCY_DECISIONS.md)(D5/D9/D10/D21)
