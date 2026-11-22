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

## 4. 이후 확장된 구조 (STEP5~6, FR-05~07)

초기 설계 위에 다음 계층이 추가됐습니다. 현재 구현 기준입니다.

| 계층 | 모듈 | 역할 |
|---|---|---|
| 이벤트 | `worker`, `iomux` | 워커 스레드별 이벤트 루프, epoll(Linux)/kqueue(macOS)/poll, accept 분산 |
| 연결 | `connection` | 비동기 feed/flush, keep-alive 100회, 접근 로그 |
| 설정 | `config` | 기본값 → `conf/tcpserver.conf` → CLI(`-p/-r/-c`) |
| 도메인 | `flight_store`, `flight_clock`, `flight_api` | 항공편 예약·취소·영속저장, 출발/도착 추적, JSON API |
| 게이트웨이 | `proxy` | 경로 prefix 라우팅 + Round-Robin, 전용 스레드, 502/504, XFF |
| 캐시 | `cdn` | 정적 자산 TTL 캐시, `X-Cache` HIT/MISS |
| 응답 | `http_response` | `Date`, `Server`, `Cache-Control`, `ETag` |

서비스 경계와 HTTP/2 처리 방침은 `eungi/DECISIONS.md`의 D26~D27을 따릅니다.

상세 데이터 흐름은 [`verify/ARCHITECTURE_REVIEW.md`](../verify/ARCHITECTURE_REVIEW.md)와 README의 아키텍처 도식을 참고합니다.
