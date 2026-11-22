# Architecture Design

본 문서는 외부 라이브러리 없이 C11과 POSIX API만으로 구현된 웹 서버의 아키텍처 및 모듈 설계를 다룹니다.

## 1. 시스템 아키텍처 개요
서버는 각 계층(Layer)이 명확히 분리된 구조를 가지며, 소켓 관리부터 애플리케이션 로직까지 데이터가 흐르는 파이프라인 구조를 따릅니다.

### 1.1 데이터 흐름 (Data Flow)
```text
[Network] -> [Server] -> [Connection] -> [Parser] -> [Handler] -> [Response] -> [Network]
   |            |            |             |            |            |
  TCP          Accept       Buffer       HTTP Spec    Static/CGI    Build Msg
```

## 2. 핵심 모듈 설계 (Module Responsibility)

### 2.1 Server & Connection (Network Layer)
- **tcpserver**: 리스닝 소켓을 생성하고 클라이언트 접속을 수락하는 Entry Point 역할을 수행합니다.
- **connection**: 수락된 연결의 생명주기를 관리합니다. 수신 데이터 버퍼링과 파싱/핸들러 호출을 중계합니다.

### 2.2 Parser & Handler (Application Layer)
- **http_parser**: 수신된 바이트 스트림을 HTTP/1.1 규격에 맞춰 구조화합니다. (Stateless Parser)
- **static_handler**: 요청 URI를 기반으로 파일 경로를 검증하고, 보안 검사(Path Traversal 방어)를 수행한 뒤 파일 리소스를 확보합니다.

### 2.3 Response (Presentation Layer)
- **http_response**: 데이터 및 상태 코드를 기반으로 표준 HTTP 응답 메시지를 생성합니다.

---

## 3. 주요 엔지니어링 포인트

### 3.1 관심사 분리 (Separation of Concerns)
- 소켓 IO와 비즈니스 로직(파싱/파일 읽기)을 분리하여 모듈 간 결합도를 낮췄습니다.
- 파서는 파일 디스크립터(FD)를 직접 다루지 않고, 메모리 버퍼만을 대상으로 동작합니다. (Testability 확보)

### 3.2 리소스 관리 정책
- **RAII 패턴**: 모든 연결 리소스는 에러 발생 시 단일 지점(connection_close)에서 안전하게 해제되도록 설계하여 누수를 방지합니다.
- **보안**: `realpath`를 활용한 경로 정규화를 통해 루트 디렉터리 상위로의 접근을 물리적으로 차단합니다.

## 4. 레이어를 쌓아 확장한 구조 (STEP1 → STEP6, FR-05~07)

초기 정적 서버 위에 한 겹씩 얹었습니다. 각 단계는 아래 레이어의 인터페이스를 그대로 쓰고, 위에 새 책임만 추가했습니다.

### L0. 기반 — 정적 서버 (STEP1~4)

`tcpserver` → `connection` → `http_parser` / `static_handler` → `http_response`

- 소켓 열기, HTTP 파싱, 경로 검증, MIME, 청크 전송, 404/403 처리까지.
- 이 시점에는 공유 상태도, 워커도 없었습니다.

### L1. 동시성 레이어 (STEP5)

`worker`, `iomux`를 추가했습니다.

- 공유 listen fd를 nonblocking으로 두고, 이벤트당 accept 1건으로 워커 간 분산.
- `connection`을 `connection_feed()` / `connection_flush()` 상태 머신으로 바꾸고 keep-alive 최대 100회, 유휴 5초, 접근 로그를 얹었습니다.
- 여기서 좌석처럼 여러 워커가 동시에 만지는 상태가 생깁니다.

### L2. 도메인 레이어 (STEP6)

`flight_store`, `flight_clock`, `flight_api`를 추가했습니다.

- L1의 워커가 만드는 경쟁을 그대로 이용해 `seat_left` before/after 재현.
- 단일 뮤텍스 → 항공편별 락 + 메타 락으로 다듬고, 파일 저장은 스냅샷 후 락 밖에서 기록.
- 예약/취소/내 예약/운항 이력과 출발·도착 추적이 여기서 동작합니다.

### L3. 게이트웨이 레이어 (FR-07)

`config`로 라우트를 읽고 `proxy`가 경로 prefix 라우팅 + Round-Robin을 맡습니다.

- 프록시를 전용 스레드 + 큐로 분리해, 느린 백엔드가 L1의 이벤트 루프를 막지 않게 했습니다.
- `X-Forwarded-For`, 502/504, 백엔드 타임아웃도 이 레이어 책임입니다.

### L4. 캐시·관측 레이어

`cdn`과 `http_response` 확장, 내장 엔드포인트를 얹었습니다.

- 정적 자산 TTL 캐시, `X-Cache: HIT/MISS`, `Cache-Control`, `ETag`.
- `Date`, `Server` 헤더와 `/health`, `/echo`, 접근 로그(`[wN]`, IP, 지연시간)로 관측성 완성.

### 위에 얹힌 결정

- HTTP/2는 C에 직접 넣지 않고 게이트웨이에서 종료합니다(D26).
- 항공편 도메인은 C 안에 두고, 캐시·DB·프론트만 별도 서비스/산출물로 연결합니다(D27).
