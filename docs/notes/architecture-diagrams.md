# 아키텍처 그림 (Obsidian 붙여넣기용)

이 파일은 옵시디언에 그대로 붙여넣으면 되는 mermaid 그림 모음이야.
코드 기준은 `eungi-dev`(로컬 최신)야.

---

## 1. 전체 아키텍처

```mermaid
flowchart TD
    C([client]) -->|TCP| L[listen fd 공유]
    L --> W0[worker 0]
    L --> W1[worker 1]
    L --> W2[worker 2]
    L --> W3[worker 3]

    W0 --> CX[connection 상태 머신]
    W1 --> CX
    W2 --> CX
    W3 --> CX

    CX --> R{경로 분기}
    R -->|정적| S[static_handler]
    R -->|/api| F[flight_api]
    R -->|프록시| Q[proxy 큐]

    S --> CDN[cdn TTL 캐시]
    F --> ST[flight_store + 뮤텍스]
    Q --> PT[프록시 전용 스레드]
    PT -->|HTTP| B([별도 백엔드 프로세스])

    S -->|send| C
    F -->|send| C
    CDN -->|send| C
```

---

## 2. 워커 하나의 이벤트 루프

```mermaid
flowchart TD
    A[iomux_wait 200ms] --> B{준비된 이벤트}
    B -->|없음| C[유휴 연결 정리]
    C --> A
    B -->|listen fd| D[accept 한 번]
    D --> E[connection_init]
    E --> F[iomux_add READ]
    F --> A
    B -->|conn READ| G[recv]
    G --> H[connection_feed]
    H --> I{보낼 게 남았나}
    I -->|예| J[iomux_mod READ+WRITE]
    I -->|아니오| A
    J --> A
    B -->|conn WRITE| K[connection_flush]
    K --> L{다 보냈나}
    L -->|예| M[iomux_mod READ]
    L -->|아니오| A
    M --> A
```

---

## 3. 커널 입장: 연결이 들어오는 길

```mermaid
flowchart LR
    C1([client A]) -->|3-way handshake| L[listen fd]
    C2([client B]) -->|3-way handshake| L
    L -->|accept| C1F[conn fd A 워커가 소유]
    L -->|accept| C2F[conn fd B 워커가 소유]
    C1F --> B1[커널 수신 큐 A]
    C2F --> B2[커널 수신 큐 B]
```

---

## 4. 왜 멀티플렉싱인가 (3가지 방식 비교)

```mermaid
flowchart TD
    subgraph BL[블로킹: 한 연결 = 한 스레드]
        B1[스레드 A: recv 대기] 
        B2[스레드 B: recv 대기]
        B3[스레드 C: recv 대기]
    end
    subgraph TH[스레드 풀: 큐에서 나눠 처리]
        T0[큐] --> T1[스레드 A]
        T0 --> T2[스레드 B]
    end
    subgraph IO[멀티플렉싱: 한 스레드가 준비된 것만]
        I0[iomux_wait] --> I1[준비된 fd만 처리]
        I1 --> I0
    end
```

---

## 5. STEP별로 무엇이 추가됐나

```mermaid
flowchart LR
    S1[STEP01 TCP] --> S2[STEP02 파서]
    S2 --> S3[STEP03 응답]
    S3 --> S4[STEP04 정적 파일]
    S4 --> S5[STEP05 동시성]
    S5 --> S6[STEP06 항공편 데모]
```

| STEP | 추가된 파일 | 추가된 것 |
|---|---|---|
| 01 | `tcpserver.c` | `socket`/`bind`/`listen`/`accept` |
| 02 | `http_parser.c` | 요청줄·헤더·본문 파싱, `req.consumed` |
| 03 | `http_response.c` | 상태줄+헤더 직렬화, 청크 전송 |
| 04 | `static_handler.c` | 경로 탈출 차단, MIME, 404/403/500 |
| 05 | `iomux.c`, `worker.c` | 워커별 이벤트 루프, keep-alive, 유휴 정리 |
| 06 | `flight_store.c`, `flight_api.c`, `proxy.c`, `cdn.c` | 좌석 예약, 프록시 게이트웨이, TTL 캐시 |

---

## 6. 코드에서 그림 대신 볼 위치

| 무엇을 보려면 | 파일 |
|---|---|
| 연결 상태 머신 | `src/connection.c` |
| 이벤트 루프 | `src/worker.c` |
| epoll/kqueue/poll 선택 | `src/iomux.c` |
| 설정 로드 | `src/config.c` |
| 정적 파일 경로 검증 | `src/static_handler.c` |
| 좌석 동시성 | `src/flight_store.c` |
| 프록시 큐 | `src/proxy.c` |
