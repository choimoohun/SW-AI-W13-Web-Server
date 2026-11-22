# VERIFICATION_LOG — 구현·검증 기록

이 문서는 오늘 구현한 내용과 실제 실행으로 검증한 결과를 구조화해 기록한다.
(원시 대화 전체가 아니라 발표·재현에 필요한 검증 근거 요약. 원시 대화는 Hermes 세션 히스토리에 있음.)
README.md의 "구현 마일스톤 및 진행 상태"와 짝을 이룬다.

## 범위

- 오늘 범위: 정적 파일 서빙 완성 (마일스톤 1~4).
  TCP 수락 → HTTP 요청 파싱 → 정적 파일 찾기 → HTTP 응답 전송(바이너리 무결성).
- 제외: Proxy, CGI, Keep-Alive, 동시성(FR-06), 부하 테스트 (이후 단계).

## 구현 완료 모듈 (마일스톤 1~4)

| 모듈 | 파일 | 책임 |
|---|---|---|
| tcpserver | src/tcpserver.c | getaddrinfo→socket→SO_REUSEADDR→bind→listen→accept |
| http_parser | src/http_parser.c | 순수 파서: 요청라인/헤더/본문, COMPLETE/NEED_MORE/ERROR |
| static_handler | src/static_handler.c | 경로 탈출 방어(realpath 경계), MIME, 404/403/500, "/"→index.html |
| http_response | src/http_response.c | 상태줄+헤더 직렬화, send_all, 16KB 청크 전송 |
| connection | src/connection.c | read 누적→파싱→정적파일→응답→단일 close, 5초 타임아웃 |
| main | src/main.c | 시그널 정책(SIGPIPE 무시, sigaction SA_RESTART 끔), accept 루프, doc_root |

## 검증 결과 (실제 실행)

### 빌드
- `make` → exit 0. `-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L` 통과.

### 단위 테스트 (`make test`)
- test_parser: all passed (요청라인/헤더/본문, 400/405/411/413/414/501/505, short read, 대소문자 무시, 중복 CL 거부).
- test_static: all passed (경로 탈출 403, 퍼센트 인코딩 탈출 403, MIME, 404, "/a..b"는 정상 이름).

### 기능(통합) 테스트 (`make integration`)
- GET / → 200 text/html + index.html
- GET /index.html, /style.css, /sample.txt → 200 (MIME 매핑)
- GET /nope → 404
- GET /../etc/passwd, /%2e%2e/etc/passwd → 403 (탈출 차단)
- POST /(본문) → 405
- GET /test.png → 원본 바이트 일치(cmp), image/png, Content-Length 149
- 10개 항목전부 PASS.

### 메모리 검사 (`make asan`, macOS)
- ASAN+UBSan 빌드 후 기능 시나리오 실행 → 메모리 오류/UB 리포트 없음(halt_on_error=1, 중단 없음).
- graceful shutdown: SIGTERM → exit 0.
- **플랫폼 제약:** macOS ASAN은 메모리 오류·UBSan만 지원하고 누수 검사(LeakSanitizer)는 Linux 전용.

### 누수 검사 (Linux Valgrind, Docker) — 확정
- Docker(debian bookworm, valgrind 3.19) 컨테이너에서 빌드 → `valgrind --leak-check=full --show-leak-kinds=all` 실행.
- 정적 파일 시나리오(GET /, 404, 403, PNG, POST 405) 실행 후 SIGTERM 종료.
- HEAP SUMMARY: in use at exit **0 bytes in 0 blocks**. total heap usage: 1 allocs, 1 frees, 64 bytes.
- "All heap blocks were freed -- no leaks are possible". ERROR SUMMARY: **0 errors**. valgrind exit 0.
- → **누수 0 bytes, 메모리 오류 0건 확정 (NFR-01 충족).**

## 테스트 실행 방법

```sh
make            # 빌드
make test       # 단위 테스트 (파서, 경로보안/MIME)
make integration# 기능 테스트 (서버 기동 후 curl 검증, 자동 종료)
make asan       # ASAN+UBSan 빌드 (macOS=메모리오류 검사)
# 누수 0 확정 (Linux/Docker):
open -a Docker  # Docker Desktop 데몬 실행 후
docker compose run --rm dev sh -c "cd /workspace && make && valgrind --leak-check=full ./webserver ..."
```

## 남은 항목

- 마일스톤 5: 누수 0 bytes 확정 완료(Linux valgrind). 부하 테스트(wrk/hey)는 동시성 구현 후.
- 동시성(FR-06) 모델 결정: 현재 단일 스레드 블로킹. thread pool vs poll 확정 → DECISIONS.md 기록.
- 부하 테스트는 동시성 구현 후 meaningful.
- 환경: 프로젝트가 iCloud 동기화 Desktop 안 → 파일 되돌림/placeholder 위험. ~/dev 등 밖으로 이동 권장.
- GitHub push / Milestone 구성(선택).

## 확정/제안 결정 (DECISIONS.md 기록 대상)

- 정적 경로에 POST → 405 (정적 서빙은 GET 의미론). [구현 반영, 확정 기록 권장]
- Transfer-Encoding: chunked → 501 (미지원). [구현 반영]
- Keep-Alive 미지원: Connection: close, 요청 1건마다 닫기. [구현 반영]
- 동시성 모델: 미결정 (thread pool 권장 vs poll). [확정 필요]

## 빌드 이슈 수정 기록

- Makefile 링크 규칙에 $(CFLAGS) 누락 → ASAN 빌드 시 런타임 미링크(undefined ___asan_*). 링크에 $(CFLAGS) 포함하도록 수정.
- test_static.c의 mkdtemp가 macOS에서 미선언 → mkdir+pid 기반 임시 디렉터리로 포터블하게 교체.
- integration 스크립트 고정 sleep 1 → ASAN 빌드 느린 시작 대응 위해 준비-대기 폴링으로 교체.
- Linux gcc에서 realpath 미선언(_POSIX_C_SOURCE=200809L만으로는 glibc XOPEN 확장 미노출) →
  피처 매크로를 _XOPEN_SOURCE=700으로 교체(Makefile·CMakeLists·compile_commands.json). macOS/Linux 모두 빌드 통과.
- Docker 컨테이너 빌드 시 호스트 macOS .o가 복사돼 링크 실패(file format not recognized) → 복사본에서 make clean 후 빌드.

---

## 2026-11-22 추가 기록 — STEP5 동시성 코어 + STEP6 항공편 데모

### 범위
- STEP5: poll 기반 멀티리액터, 논블로킹 연결 상태 머신, 워커 풀, keep-alive, 유휴 타임아웃, graceful shutdown.
- STEP6: 인메모리 항공편 저장소 + JSON API + 정적 보드 UI. 좌석 경쟁 before/after, 출발/도착 상태·타임라인.
- 이전 로그의 "동시성 미결정 / Keep-Alive 미지원" 항목은 이 기록으로 대체된다.

### 구현 모듈
| 모듈 | 파일 | 책임 |
|---|---|---|
| iomux | src/iomux.c | POSIX poll 백엔드. add/mod/del/wait, fd→컨텍스트 ptr 매핑 |
| connection | src/connection.c | feed/flush 상태 머신, keep-alive, 파일 청크 스트리밍, 단일 destroy |
| worker | src/worker.c | 워커 4개, 공유 listen nonblocking accept(이벤트당 1건), 유휴 5초 정리 |
| http_parser | src/http_parser.c | consumed 필드 추가(keep-alive 요청 경계) |
| http_response | src/http_response.c | build_json/error/file 직렬화(소켓 쓰기와 분리) |
| flight_store | src/flight_store.c | 시드 3편, 전역 뮤텍스, before/after 토글, 타임라인 |
| flight_api | src/flight_api.c | 최소 JSON 파서/직렬화, API 라우팅 7개, 버퍼 응답 |
| main | src/main.c | 워커 풀 배선, SIGTERM → join → close, WEB_WORKERS 설정 |

### 검증 결과 (실제 실행, macOS arm64)
- `make`, `make test`, `make integration`: 기존 10개 항목 전부 PASS(회귀 없음).
- `make test-flight`(스레드 32, KE999 1석): after 성공 1건/32, 잔여 0. before 성공 32건/32, 잔여 -31.
- `make integration-flight`:
  - 기본 API 200/404/400/405, 상태 변경 200, 역행 400, 타임라인 기록 PASS.
  - keep-alive: 헤더 `Connection: keep-alive`, 한 연결 두 요청 `200 200`.
  - slow client: 헤더 일부만 보낸 연결이 있어도 다른 curl이 2초 내 200.
  - before(HTTP 100동시): `201 4 / 409 96` — 3회 연속 재현.
  - after(HTTP 100동시): `201 1 / 409 99`, 잔여 좌석 0.
- `make tsan`: after 경고 0건, before `WARNING: ThreadSanitizer: data race` 4건(`flight_store.c:178`).
- 종료: SIGTERM → 워커 join → listen fd close, 잔여 프로세스 없음.

### 재현 명령
```sh
make && ./webserver                       # 기본 after, 워커 4
make test-flight                          # 스토어 레이스 단위 검증
make integration-flight                   # HTTP 100동시 + keep-alive + slow client
make tsan && ./webserver                  # before/after 데이터 레이스 비교
```

### 발견한 함정 (기록)
- 워커가 accept를 EAGAIN까지 반복하면 첫 워커가 100개 연결을 독점해 같은 스레드에서 직렬 처리되고, before 모드에서도 초과 예약이 발생하지 않았다. 이벤트당 1회 accept로 분산한 뒤 해결.
- before 모드 레이스 창을 200µs로 두면 HTTP 동시 요청에서 재현이 불안정해 20ms로 확대.
- macOS SDK에서 `pthread_barrier_t`가 노출되지 않아 단위 테스트는 `volatile int go` 스핀 게이트 사용.

### 남은 항목
- epoll/kqueue 백엔드 미구현(poll만 사용). 대규모 fd 처리량 필요 시 추가.
- 누수 0 확정은 이전 Linux valgrind 기록 유지. 새 워커 코드에 대한 valgrind 재검증은 Docker에서 수행 필요.
- iCloud 동기화 폴더에서 `eungi/DECISIONS.md`가 작업 중 예전 내용으로 되돌아간 적 있음(재반영 완료). 프로젝트를 ~/dev 밖으로 이동 권장.

---

## 2026-11-22 추가 기록 (2) — 실서비스형 예약 앱으로 전환

### 요구사항 재정의
- 데모용 UI(모드 토글, 동시 예약 버튼, 강제 지연 버튼)는 모두 제거했다.
- 앱은 항공권 서비스처럼 보이고 동작해야 한다: 검색/상세/예약/내 예약/취소/운항 이력.
- 좌석 경쟁 before/after는 앱 밖(터미널 검증, TSan)에서만 확인한다.

### 구현
| 항목 | 내용 |
|---|---|
| 화면 | `www/flights.html`, `www/flights.js` — 출발/도착 검색, 상세+예약 폼, 내 예약 목록, 취소, 운항 이력(읽기 전용) |
| 사용자 식별 | 브라우저 localStorage에 client id 발급. `client` 쿼리/본문으로 전달(인증 아님) |
| 저장 | `data/flights.txt`, '|' 구분 텍스트. F/R/E 레코드. 임시 파일 → rename 원자적 교체 |
| 신규 API | GET `/api/flights?from=&to=`, GET/POST `/api/reservations`, POST `/api/reservations/{id}/cancel` |
| git 추적 | `FLIGHT_DATA_FILE` 환경변수로 경로 변경. `/dev/null`이면 비영속(테스트용) |

### 검증 결과
- `make test-flight`: after 1건/32(잔여 0), before 32건/32(잔여 -31).
- `make integration-flight`: 기본 API, 예약 생성/연락처 저장/내 예약 조회/소유자 확인 취소/좌석 복원, keep-alive, slow client, before `201 4 / 409 96`, after `201 1 / 409 99` 전부 PASS.
- `make integration-persist`: 예약 후 서버 재시작 → 예약·승객 정보·좌석·운항 이력 유지 6개 항목 전부 PASS.
- 실제 브라우저에서 항공편 선택 → 예약 → 내 예약 조회 → 취소까지 확인. 취소 후 `seat_left` 3으로 복원 확인.
- `make tsan`은 워커 코어 변경 전 기록 유지(새 영속 코드 포함 재실행은 Docker/Linux 권장).

### 발견한 버그 (수정 완료)
- `split_fields()`가 구분자 뒤 `dst`를 증가시키지 않아 이전 필드의 종료 NUL을 덮어써서 저장 파일 로드가 0건으로 읽힘.
- `store_load()`가 `g_store`를 memset하면서 이미 초기화된 뮤텍스를 지워버림 → 뮤텍스 초기화를 로드 뒤로 이동.

### 남은 항목
- 인증/결제 없음. client id는 브라우저 로컬 값이라 실서비스 수준의 소유권 보호는 아님.
- epoll/kqueue 백엔드, 새 워커 코드의 Linux valgrind 재검증은 미완.

---

## 2026-11-22 추가 기록 (3) — 출발/도착 시간 자동 추적

### 구현
| 항목 | 내용 |
|---|---|
| 시계 | `src/flight_clock.c`. `FLIGHT_TIME_MODE=real|sim`, `FLIGHT_SIM_SPEED`(기본 600). CLOCK_REALTIME 밀리초 기준, 출발 10분 전 앵커 |
| 상태 전이 | `flight_store_tick(now)`. SCHEDULED/BOARDING/DELAYED → DEPARTED → ARRIVED. 실제 출발/도착 시각 + 이벤트 기록, 영속 저장 |
| 구동 | 워커 0번의 200ms poll 타임아웃에서만 호출(중복 이벤트 방지) |
| 지연 API | `POST /api/ops/flights/{id}/delay` `{"delay_min":30}`. 예정 출발/도착 +30분, `delay_applied_min` 누적, 이벤트 기록 |
| 시계 API | `GET /api/ops/clock` → now/mode/speed |
| 화면 | `www/ops.html` + `www/ops.js`. 운항 현황 표, 카운트다운, 실제 출발/도착, 지연 입력, 운항 이력 |
| 포트 | `WEB_PORT` 환경변수 지원. 추적 통합 테스트는 8081로 격리 |

### 검증 결과
- `make test-tracking`: sim 시계 0.2초에 201~262가상초 진행. tick 경계 전이/멱등, 지연 후 밀린 시각에 출발, 이벤트 1건씩 전부 PASS.
- `make integration-tracking`(배속 3000, 포트 8081): sim 모드·배속, 지연 30분 반영과 이벤트, KE701 자동 출발·도착과 실제 시각 기록, 이벤트 2건 전부 PASS.
- 전체 회귀: `make test`, `test-flight`, `test-tracking`, `integration`, `integration-flight`, `integration-persist`, `integration-tracking` 전부 PASS. 잔여 서버 프로세스 없음.

### 설계 주의
- tick은 반드시 한 스레드에서만 호출한다. 워커 4개가 각자 호출하면 같은 이벤트가 4번 기록된다.
- 예정 시각 지연(`delay_applied_min`)과 실제 지연(`delay_min`)은 별도 필드로 둔다. 출발 전에도 지연 분을 화면에 표시할 수 있어야 하기 때문이다.



---

## 2026-11-22 추가 기록 (4) — 남은 요구사항 + 미니 CDN

### 외부 CDN 사용 가능 여부에 대한 결론

이 서버는 로컬에서 실행되는 C11/POSIX 바이너리이고 공개 도메인·DNS·TLS 종료·엣지 PoP가 없다.
따라서 Cloudflare/CloudFront 같은 **외부 CDN을 이 아키텍처에 직접 붙일 수는 없다.**
대신 CDN의 핵심 동작만 서버 안에서 재현했다.

| 항목 | 구현 |
|---|---|
| TTL 캐시 | `src/cdn.c`, 슬롯 32개 · 슬롯당 16KB · `cdn_ttl`(기본 60초) |
| 히트/미스 확인 | 정적 파일 응답에 `X-Cache: HIT` / `X-Cache: MISS` 헤더 |
| 무효화 | `cdn_purge(key)` API 제공(현재는 TTL 만료로 자동 정리) |
| 검증 | 같은 URL 두 번 요청 → MISS → HIT 확인 |

### 구현·수정

- `src/config.c`: `trim()`이 앞쪽 공백을 포인터만 이동시키고 있어 `route = /api/...`의 prefix에
  앞 공백이 남아 라우팅이 전부 404로 떨어졌다. 실제 버퍼를 `memmove`하도록 수정.
- `src/proxy.c`: prefix 매칭 게이트웨이 + 다중 백엔드 Round-Robin 로드밸런서.
  `X-Forwarded-For` 추가, 실패 502, 응답 지연 504.
- `src/cdn.c`, `include/cdn.h`: 미니 CDN.
- `src/connection.c`: 프록시 분기, CDNMISS/HIT 분기, 정적 본문 캐시 적재.
- `src/http_response.c`: `http_response_build_file`에 `extra_headers` 추가(`X-Cache`).
- `tests/integration/fixtures/backend.py`: 프록시 검증용 백엔드(포트/경로/XFF 반환).
- `tests/integration/requirements.sh`, `tests/integration/load_test.sh`: 신규 검증 스크립트.
- `Makefile`: `integration-req`, `load-test` 타겟 추가.

### 검증 결과 (실제 실행)

| 명령 | 결과 |
|---|---|
| `make integration-req` | Date/Server 헤더, CDN MISS→HIT, `/health`, `/echo`, 프록시 8401↔8402 Round-Robin, XFF 전달, 백엔드 없음 502, 접근 로그(worker/IP/ms) 전부 PASS |
| `make test` | `test_parser`, `test_static` PASS |
| `make test-flight` | after 1/32(잔여 0), before 32/32(잔여 -31) PASS |
| `make integration` | 404/403/405/바이너리/MIME PASS |
| `make integration-flight` | 예약·취소·keep-alive·slow client·before/after PASS |
| `make integration-persist` | 재시작 후 예약/좌석/이력 유지 6항목 PASS |
| `make test-tracking` / `make integration-tracking` | 시계 전이·지연·자동 출발/도착 PASS |
| `make load-test` | 200요청/동시 20, 성공 200, Non-2xx 0, 352ms, 약 568 RPS |

### 남은 항목

- 실제 외부 CDN 연동이 필요하면 공개 배포(DNS + TLS + 오리진)가 별도 프로젝트로 필요하다.
- `hey`/`wrk` 설치 후 percentile(latency p50/p95/p99) 측정 가능. 현재는 curl 병렬 근사값.
- epoll/kqueue 백엔드와 새 코드의 Linux valgrind 재검증은 미완.


---

## 2026-11-22 추가 기록 (5) — 남은 5개 항목 완료

### 실제 수정
- `src/iomux.c`: Linux epoll / macOS kqueue 백엔드 추가, poll 폴백 유지. 이벤트 배열을 256으로 키우고 wait 시 `min(max, cap)`으로 클램프( valgrind가 잡은 epoll_pwait OOB 수정 ).
- `src/proxy.c` + `src/worker.c` + `src/main.c`: 프록시를 전용 스레드+큐로 분리. `dup()` fd로 소유권 이전, 워커는 즉시 반환. 종료 시 큐 드레인 후 join.
- `src/flight_store.c`: 항공편별 `pthread_mutex_t` 추가, 메타 락은 예약/모드만 보호, 파일 저장은 스냅샷 후 락 밖에서 기록. 락 순서 flight→meta 고정.
- `src/connection.c`: 정적 응답에 `Cache-Control: public, max-age=` 및 약한 `ETag` 추가.
- `Makefile`: `-MMD -MP` 헤더 의존성 추가(구조체 레이아웃 불일치 버그 재발 방지).
- `usleep` → `nanosleep`으로 교체해 Linux glibc 빌드 통과.

### 검증 결과
| 항목 | 결과 |
|---|---|
| `make` / `make test` | PASS |
| `make test-flight` | after 1/32(잔여 0), before 32/32(잔여 -31) PASS |
| `make integration-flight` / `integration-persist` | PASS |
| `make test-tracking` / `integration-tracking` | PASS |
| `make integration-req` | Cache-Control/ETag, CDN HIT, 프록시 RR, XFF, 502, 접근 로그 PASS |
| 프록시 블로킹 검증 | 느린 백엔드 2065ms 동안 `/health` 0ms → 워커 블로킹 없음 PASS |
| `make load-test` | 200요청/동시 20, 성공 200, Non-2xx 0, 약 530 RPS |
| `make leak` (Docker/Linux valgrind) | in use at exit 0 bytes, All heap blocks freed, ERROR SUMMARY 0 errors, exit 0 PASS |
| TSan 재검증 | after data race 0건, before 1건 검출(`flight_store.c:420`) |

### 주의
- Docker 호스트 디스크가 100% 차면서 Docker VM이 read-only가 된 적 있음. 백그라운드 프로세스 강제 종료 후 재시작해서 해결.
- valgrind 시나리오는 정적/CDN/health/echo/항공편/예약/타임라인/프록시 502 경로를 모두 포함.
