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

## 4. 오류를 만날 때마다 레이어를 얹은 구조 (STEP1 → STEP6, FR-05~07)

실제로 깨진 지점을 따라 한 겹씩 얹었습니다.

| 레이어 | 실제로 드러난 문제 | 그래서 얹은 것 |
|---|---|---|
| L0 기반 (STEP1~4) | 정적 파일 경로 탈출, 바이너리 손상, MIME 누락 | `realpath` 경계 검사, 청크 전송, `http_parser` / `static_handler` / `http_response` 분리 |
| L1 동시성 (STEP5) | 단일 accept 루프가 100요청을 직렬 처리 → before 모드에서도 초과 예약이 안 나옴 | `worker` + `iomux`, 공유 listen fd, 이벤트당 accept 1건 |
| L1 연결 | keep-alive에서 `req.consumed`를 안 지워 recv_buf에 다음 요청이 섞임 | `connection_feed()` 버퍼 정리, 최대 100회, 유휴 5초 |
| L2 도메인 (STEP6) | `split_fields()`가 구분자 뒤 포인터를 안 늘려 저장 파일이 0건으로 로드됨 | 필드 파서 수정, `data/flights.txt` 영속 저장 |
| L2 락 | `store_load()`의 `memset`이 이미 초기화한 뮤텍스를 지움 | 락 초기화 순서 변경 → 항공편별 락 + 메타 락 |
| L2 락 | 락을 든 상태에서 파일 쓰기 → 디스크 I/O가 예약을 막음 | 스냅샷을 만든 뒤 락 밖에서 `rename()` 기록 |
| 빌드 | `flight_t`에 뮤텍스 추가 후 헤더 의존성이 없어 객체가 안 바뀜 → 구조체 레이아웃 불일치로 KE999가 404 | `-MMD -MP` 헤더 의존성 추가 |
| L3 게이트웨이 (FR-07) | `trim()`이 앞 공백을 포인터만 옮겨 prefix가 `" /api/orders/"`가 됨 → 모든 라우트 404 | `memmove`로 실제 버퍼를 당김 |
| L3 게이트웨이 | 프록시의 blocking recv가 워커 전체를 멈춤 → 느린 백엔드가 `/health`까지 막음 | 프록시 전용 스레드 + 큐, `dup()`로 소유권 이전 |
| L4 캐시·관측 | `X-Cache`만 있고 `Cache-Control`/`ETag`가 없어 실제 CDN 캐시 정책 판단 불가 | 캐시 헤더 추가, `Date`/`Server`, 접근 로그(`[wN]`, IP, 지연) |
| L4 검증 | valgrind가 `epoll_pwait(events)` OOB 검출(배열 64, 요청 max 256) | 이벤트 배열 256으로 확대, `min(max, cap)` 클램프 |
| 이식성 | Linux glibc에서 `usleep` 미선언 → `-Werror` 빌드 실패 | `nanosleep()`으로 교체 |

마지막으로 구현하지 않은 것도 결정으로 남겼습니다.

- HTTP/2는 C 파서를 다시 만들지 않고 게이트웨이에서 종료합니다(D26).
- 항공편 도메인은 C 안에 두고, 캐시·DB·프론트만 별도 서비스/산출물로 연결합니다(D27).
