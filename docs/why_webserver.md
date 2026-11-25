# 웹서버가 푸는 중심 문제

이 문서는 이 프로젝트가 왜 존재하고 어떤 문제를 푸는지 정리합니다.
문제 정의는 [REQUIREMENTS.md](REQUIREMENTS.md),
동시성·멀티플렉싱 근거는 [notes/iomux.md](notes/iomux.md),
설계는 [DESIGN.md](DESIGN.md), 실행 증거는 [verify/](verify/)에 있습니다.

## 역사적 불편과 이 프로젝트의 범위

웹서버가 풀어 온 불편은 층이 다릅니다. 기준은 하나입니다.
**이 프로세스 안에서 일어나는가.** 이 서버 안 문제부터 확장, 범위 밖 순서로 적습니다.

### 이 서버 안

| 불편한 문제 | 요구사항 | 구현 | 확인 |
|---|---|---|---|
| 요청마다 프로세스를 만든다 | FR-06 안 A 워커 풀 | `worker.c`가 스레드 4개를 미리 만들어 돌린다 | `make integration-flight`, `make load-test` |
| 느린 클라이언트·백엔드가 워커를 점유한다 | FR-06 동시성, NFR-03 타임아웃 | `iomux.c`가 스레드를 잠들지 않게 하고, `worker.c`의 `cleanup_idle`이 유휴 연결을 닫는다 | `requirements.sh`의 slow vs health, 504 |
| 좌석을 두 명이 같은 순간에 잡는다 | FR-06 동시성 | `flight_store.c`가 항공편별 뮤텍스로 읽기와 감소를 하나로 묶는다 | `tests/unit/test_flight_race.c` |
| 한 프로세스에 다 넣으면 배포·장애·확장이 같이 묶인다 | FR-07 리버스 프록시 | `config.c`의 `config_match_route`가 경로로 나누고, `proxy.c`가 큐와 전용 스레드로 보낸다 | `requirements.sh`의 RR 분산, 502 |
| 백엔드 여러 대에 분산해야 한다 | Should Round-Robin | `proxy.c`의 `g_rr`이 1→2→1→2 순서를 만든다 | `requirements.sh`의 1→2→1→2 |
| 연결을 매번 끊으면 핸드셰이크가 반복된다 | Should keep-alive, NFR-03 타임아웃 | `http_response.c`가 keep-alive 헤더를 붙이고, `connection.c`가 요청 수와 유휴 5초를 본다 | `flight_run.sh`의 한 연결 두 요청 |
| 정적 파일만으론 달라지는 응답을 못 만든다 | FR-04 정적 서빙, FR-05 동적 처리 | `static_handler.c`가 파일을 서빙하고, `flight_api.c`가 `/api`를 처리한다 | `requirements.sh`의 `/echo` |

### 확장 — 요구사항에 없는 것

| 불편한 문제 | 구현 | 확인 |
|---|---|---|
| 자주 읽는 정적 파일을 매번 전송한다 | `cdn.c`의 `cdn_store`가 `expires`로 TTL을 관리한다 | `requirements.sh`의 `X-Cache: MISS` → `HIT` |
| 재시작하면 예약이 사라진다 | `flight_store.c`가 스냅샷을 임시 파일에 쓰고 `rename()`한다 | `make integration-persist` |
| 유휴 연결이 자원을 점유한다 | `worker.c`의 `cleanup_idle`이 `last_active`로 판별한다 | 추가 필요 |

### 범위 밖 — 다른 층이 푸는 것

- HTTP/1.1 연결 안에서 앞 응답이 막히면 뒤도 막힌다. HTTP/2 프레임·스트림 영역.
- TCP 패킷 손실이 모든 스트림을 멈춘다. HTTP/3 QUIC 영역.
- 매 요청마다 같은 헤더를 반복 전송한다. HPACK 영역.
- 서버를 늘리면 세션 공유가 필요하다. 세션 서버·Redis·토큰 영역이며 아직 구현하지 않았다.
  다음 과제는 [docs/agent/DAILY_SCOPE.md](agent/DAILY_SCOPE.md).
- TLS 종료, 인증, 호출 제한. 앞단 장비의 일이라 별도 과제로 보류했다.

`확인`이 비어 있거나 추가 필요인 행은 아직 증명하지 못한 것입니다.
