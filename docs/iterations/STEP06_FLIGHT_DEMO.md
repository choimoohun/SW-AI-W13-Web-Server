# 동적 요청 처리

## STEP6 - 항공편 예약·출발/도착 추적

## 범위 / 의도

- 요구사항의 "동적 요청 처리"를 항공편 예약으로 구현한다.
- 입력에서 두 가지가 섞여 있었다: (1) 항공편 예약 서버(동시성 해결 전/후 데모), (2) 출발/도착 시간 추적 서버.
- 둘을 하나의 앱으로 합친다. 예약은 경쟁이 가장 치열한 지점을 만들고, 출발/도착 추적은 같은 `flight` 레코드에 상태/타임라인 필드를 얹는 것뿐이므로 별도 서버로 나눌 이유가 없다.
- 기존 웹서버를 그대로 확장한다: `www/` 정적 보드 + `/api/...` 엔드포인트. 외부 API/DB/JSON 라이브러리 없음(시드 데이터는 코드에 박아둠).
- 전제: STEP5(멀티 워커 스레드) 이후에만 의미가 있다. STEP1~4의 단일 accept 루프에서는 요청이 직렬 처리되므로 좌석 경쟁이 재현되지 않는다. "before" 모드의 레이스는 워커 스레드가 2개 이상일 때 비로소 보인다.

## 비범위

- 실제 결제/좌석 배정/인증, 영속 저장(파일·DB), 실시간 푸시(WebSocket), 다국어.
- JSON 파서는 필요한 필드만 읽는 최소 구현. 범용 파서 만들지 않기.

---

## 파일 구성

| 파일 | 책임 | 비고 |
|---|---|---|
| `include/flight_store.h` | `flight_t`, `reservation_t`, `flight_event_t`, `flight_store_t` 선언 + API | 자료구조/뮤텍스 포함 |
| `src/flight_store.c` | 시드 초기화, 좌석 예약, 조회, 상태 변경, 이벤트 기록 | 경쟁 지점 여기 |
| `include/flight_api.h` / `src/flight_api.c` | 라우팅, 요청 파싱, JSON 직렬화, 상태코드 | `http_response` 재사용 |
| `src/http_response.c` | `http_response_json()` 헬퍼 추가 | 기존 함수는 건드리지 말고 추가 |
| `src/connection.c` | `/api/` 접두사면 `flight_api_handle()`로 분기 | TODO 한 줄만 삽입 |
| `www/flights.html` / `www/flights.js` | 보드 UI: 항공편 목록, 좌석 잔량, 타임라인, 예약 버튼 | 정적 파일로 서빙 |
| `tests/integration/flight_run.sh` | 100동시 예약 + 상태코드 집계 검증 | `make integration`과 별도 실행 |
| `Makefile` | `test-flight` / `tsan` 타깃 추가 | 기존 타깃은 변경 금지 |

> main.c는 수정하지 않는다. 설정은 STEP5 배선 그대로 port 8080 / doc_root `./www` 사용.

---

## 데이터 모델 (초안 — 주석으로 의도를 남긴다)

```c
typedef enum { F_SCHEDULED, F_BOARDING, F_DEPARTED, F_DELAYED, F_ARRIVED } flight_status_t;

typedef struct {
    char id[16];                 /* "KE701" */
    char from[8], to[8];         /* "ICN", "NRT" */
    long depart_epoch;           /* time_t, UTC 초. 출발 시간 추적 */
    long arrive_epoch;           /* 도착 시간 추적. 도착이 출발보다 빠르면 입력 거부 */
    long actual_depart_epoch;    /* 0 = 아직 출발 안 함 */
    long actual_arrive_epoch;    /* 0 = 아직 도착 안 함 */
    int  seat_total;
    int  seat_left;              /* ← 예약 경쟁의 대상 */
    flight_status_t status;
} flight_t;

typedef struct { char id[32], flight_id[16], name[64]; long created_epoch; } reservation_t;
typedef struct { long ts; flight_status_t status; char note[128]; } flight_event_t;

typedef struct {
    flight_t flights[16];
    size_t flight_count;
    reservation_t reservations[256];
    size_t reservation_count;
    flight_event_t events[16][32];  /* 항공편별 타임라인 */
    size_t event_count[16];
    pthread_mutex_t mu;
    int lock_enabled;            /* 1=after(기본), 0=before(의도적 레이스) */
} flight_store_t;
```

- 동적 할당 없이 고정 배열 → ASan/valgrind 누수 추적이 깔끔해진다.
- 시드: `KE701 ICN→NRT 3석`, `KE999 ICN→JFK 1석`(경쟁용), `KE202 ICN→NRT 5석`(지연 시연용).
- 파생값 `delay_min = (actual - scheduled)/60`은 서버가 계산해 JSON에 같이 준다(클라이언트에 계산 책임 주지 않기).

---

## API 계약

| Method | Path | Body | 성공 | 실패 |
|---|---|---|---|---|
| GET | `/api/flights` | - | 200 목록 | - |
| GET | `/api/flights/{id}` | - | 200 상세 | 404 |
| POST | `/api/flights/{id}/reserve` | `{"name":"홍길동"}` | 201 예약 정보 | 404 / 400 / 409 SOLD_OUT |
| GET | `/api/flights/{id}/timeline` | - | 200 이벤트 배열 | 404 |
| POST | `/api/admin/flights/{id}/status` | `{"status":"DELAYED","note":"...", "at":<epoch>}` | 200 | 400/404 |
| GET | `/api/admin/mode` | - | 200 `{"lock":true}` | - |
| POST | `/api/admin/mode` | `{"lock":false}` | 200 | 400 |

- `Content-Type: application/json; charset=utf-8`, `Content-Length` 필수. HTTP/1.1 keep-alive 유지.
- 에러 본문은 `{"error":"SOLD_OUT","message":"..."}` 형태로 일관되게.
- `Transfer-Encoding: chunked`는 501, `Content-Length` 8KB 초과는 413.

---

## 동시성 설계

### after (기본, `lock_enabled = 1`)

1. `pthread_mutex_lock(&flight.mu)` → 좌석 확인(`seat_left > 0`) → 예약 레코드 추가 → `seat_left--` → 이벤트 기록 → `pthread_mutex_unlock(&flight.mu)`.
2. 락 안에서 응답 문자열을 만들지 않는다. 필요한 값만 지역 변수/버퍼로 복사하고 `pthread_mutex_unlock()`한 뒤 JSON 직렬화·소켓 전송을 한다(락 보유 시간 최소화).
3. 파일 저장도 락 안에서 하지 않는다. `snapshot_and_save()`가 스냅샷을 만든 뒤 락을 풀고 `rename()`으로 원자적 교체한다.
4. 읽기 API(`flight_store_list()`, `flight_store_timeline()`)도 같은 항공편 락을 사용해 찢어진 값을 읽지 않게 한다.

### before (`lock_enabled = 0`, 의도적 버그)

- 예약 경로에서만 항공편 락을 건너뛴다. `seat_left > 0` 확인 → `seat_left--` 사이에 `nanosleep(20000µs)`을 넣어 레이스 창을 인위적으로 넓힌다(데모 전용. 없으면 너무 빨라서 안 보임).
- 이건 실제 데이터 레이스(UB)다. 절대 기본값으로 두지 말고, `POST /api/admin/mode`로 `{"lock":false}`를 보내 켜고 데모 끝나면 즉시 `{"lock":true}`로 되돌린다.

### 스케일 업 메모 (지금은 하지 않는다)

- 단일 항공편 락 방식도 데모용이다. 처리량을 더 올리려면 `atomic_compare_exchange_n()` 기반 CAS나 항공편별 샤딩을 검토한다.

---

## 작업 순서 (Todo — 직접 구현하면서 체크)

- [x] 1. `flight_store.h/c` 스캐폴딩 구현: 고정 배열 저장소 + 시드 + TODO 주석을 실제 계약으로 교체.
- [x] 2. `flight_store_reserve()` 구현. after=항공편 락 / before=무락 + `nanosleep(20000µs)` 레이스 창. `FS_SOLD_OUT` 시 재고 유지.
- [x] 3. `flight_store_set_status()` 구현: 실제 출발/도착 시각 갱신, 역행·시간 역전 400, 타임라인 기록.
- [x] 4. `http_response_send_json()` 추가 (201/409 reason phrase 포함). 기존 함수 시그니처 유지.
- [x] 5. 최소 JSON 유틸: `json_get_string/long/bool`, `jb_str` 이스케이프. UTF-8 바이트 통과.
- [x] 6. `flight_api.c` 라우팅 7개 + 405/404/400/409/413/414, 본문 8KB 제한.
- [x] 7. `connection.c`에 `/api/` 분기 삽입. 정적 파일 경로는 그대로 통과.
- [x] 8. `www/flights.html/js`: 실서비스형 예약 화면(검색/상세/예약/내 예약/취소/운항 이력). 데모 컨트롤은 제거됨.
- [x] 9. `tests/unit/test_flight_race.c`, `tests/integration/flight_run.sh` 작성.
- [x] 10. `Makefile`에 `test-flight`, `integration-flight`, `tsan` 타깃 추가. 기존 타깃 회귀 없음.
- [x] 11. DECISIONS.md에 D6~D9 기록.

---

## 엣지 케이스 체크리스트

- [x] 마지막 1석에 동시 요청 → after에서 정확히 1건만 201, 나머지 전부 409. (`tests/integration/flight_run.sh`)
- [x] 좌석 0석에서 예약 → 201 없음, 재고가 음수로 내려가지 않음. (`flight_run.sh` 좌석 0석 예약 409 + `seat_left:0` 확인)
- [x] 빈 이름 → 400. (`flight_run.sh` 이름 누락)
- [x] 64자 초과 이름 → 400. (`flight_run.sh`)
- [x] 없는 항공편 ID → 404. (`flight_run.sh`)
- [x] 출발 전 도착 시각 입력 → 400. (`flight_run.sh` 도착 시각 역전)
- [x] 상태 전이 역행(ARRIVED → SCHEDULED) → 400. (`flight_run.sh`)
- [x] JSON escape: 이름에 `"`, `\` 포함 → 응답 JSON이 파싱 가능. (`flight_run.sh` python json round-trip)
- [x] Content-Length 과대 → 413. (`tests/integration/edge_cases.sh`)
- [ ] Content-Length 거짓(실제 본문보다 작게 선언) → 400 또는 연결 종료. (미검증)
- [x] 모드 토글 직후 동시 요청 → 서버 크래시 없음. (`flight_run.sh` before/after 각각 동시 요청)

---

## 검증 계획 (실행 결과로만 기록)

```sh
make clean && make && make test && make integration   # 기존 동작 회귀 없음
make asan && ./webserver                              # 메모리 오류 0
make tsan && ./webserver                              # after=clean / before=race 리포트 (지원 시)
sh tests/integration/flight_run.sh                    # 신규 통합
```

```sh
# before/after 토글
curl -s -X POST -d '{"lock":false}' localhost:8080/api/admin/mode
curl -s -X POST -d '{"lock":true}'  localhost:8080/api/admin/mode

# 마지막 1석 경쟁 (KE999)
seq 1 100 | xargs -P 100 -I{} curl -s -o /dev/null -w '%{http_code}\n' \
  -X POST -H 'Content-Type: application/json' -d '{"name":"user{}"}' \
  localhost:8080/api/flights/KE999/reserve | sort | uniq -c
```

- 기대값: before → `201` 다수 + `409` 일부(초과 예약 재현). after → `201 1건`, `409 99건`.
- 순서: 반드시 before 먼저 → after. after에서 성공 후엔 KE999가 매진 상태이므로, repeat하려면 서버 재시작(시드 초기화) 필요.
- macOS에서 TSan 미지원 시 "사용 불가"로 기록하고, 초과 예약 횟수로 레이스 존재를 증명한다.

---

## 리밋 자동 분배(t1~t4) 매핑

현재 `plan status`의 t1/t3은 입력 문구가 잘려 `"싶은 작업 너무 많이 입력했는데"`로 저장됐다. 아래처럼 재해석해서 사용한다.

| 슬롯 | 재정의 | 예산 |
|---|---|---|
| t1 | 이 문서(계획) 확정 + 스캐폴딩 파일 생성 | 3회 |
| t2 | store/api/UI 구현 (STEP6 Todo 1~8) | 6회 |
| t3 | 엣지 케이스 보강 + 문서화 (Todo 9~11) | 5회 |
| t4 | 테스트·시연 검증, DECISIONS 기록 | 2회 |

> 너무 긴 한 줄은 자동 분배가 문장 중간을 자른다. `python3 ~/.hermes/tools/rate_limit_manager.py setup` 에 한 문장짜리 목표를 다시 넣으면 깔끔하게 나뉜다. 슬롯이 끝날 때마다 `/new`로 대화를 새로 여는 게 리밋 방지에 가장 효과적이다.

## 앱 성격 변경 메모

- 최초 화면은 동시성 데모 보드였으나, 현재는 실제 예약 서비스 화면으로 교체됐다.
- before/after 좌석 경쟁은 앱 UI에서 제거하고 터미널(`make integration-flight`)과 TSan으로만 확인한다.
- 예약은 `data/flights.txt`에 영속 저장된다(재시작 후 유지, `make integration-persist`).

## 검증 결과 기록 (2026-11-22, macOS arm64)

- `make` / `make test` / `make integration`: 기존 10개 항목 전부 PASS (회귀 없음).
- `make test-flight` (스레드 32개, KE999 1석):
  - after: 성공 1건 / 32건, 잔여 좌석 0
  - before: 성공 32건 / 32건, 잔여 좌석 -31 ← 초과 예약 재현
- `make integration-flight`:
  - 기본 API 200/404/400/405, 상태 변경 200, 상태 역행 400, 타임라인 이벤트 기록 PASS
  - before(HTTP 100동시): `201 4건 / 409 96건` ← 초과 예약 재현 (3회 연속 동일)
  - after(HTTP 100동시): `201 1건 / 409 99건`, 잔여 좌석 0
- `make tsan` (실제 워커 4개, 12~40동시 요청):
  - before: `WARNING: ThreadSanitizer: data race` 1건(`flight_store.c:420`)
  - after: 0건
- STEP5 워커 풀 완성으로 임시 `FLIGHT_DEMO_MT` 경로 제거. keep-alive/슬로우 클라이언트 검증 추가.

## 관련 결정
- D6~D28 가운데 도메인 관련 결정 — [STEP06 결정](../decisions/STEP06_FLIGHT_DEMO_DECISIONS.md)

## 기록 원칙

- 구현하지 않은 기능을 완료로 기록하지 않는다. 검증은 실행 결과(상태코드 집계, 테스트 출력)로만 남긴다.
- before 모드 결과는 "의도된 버그 재현"으로 명시하고, after 결과와 나란히 적는다.
