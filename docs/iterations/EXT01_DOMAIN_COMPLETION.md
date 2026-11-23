# EXT01 — 항공편 도메인 완성

STEP6에서 만든 항공편 뼈대를 실제 서비스 수준으로 마무리한 단계입니다. 목표는
"예약이 겹치지 않고, 재시작 후에도 남고, 출발/도착 상태를 추적할 수 있는 저장소"입니다.

## 실제로 드러난 문제와 얹은 레이어

| 문제 | 원인 | 조치 |
|---|---|---|
| 저장 파일이 0건으로 로드됨 | `split_fields()`가 구분자 뒤 포인터를 늘리지 않음 | 필드 파서 수정, `data/flights.txt` 영속 저장 |
| 락 초기화가 깨짐 | `store_load()`의 `memset`이 이미 초기화한 뮤텍스를 지움 | 락 초기화 순서 변경, 항공편별 락 + 메타 락 |
| 예약이 디스크 때문에 막힘 | 락을 든 상태에서 파일 쓰기 | 스냅샷 복사 → 락 해제 → 임시 파일 → `rename()` |
| KE999가 404 | `flight_t`에 뮤텍스 추가 후 헤더 의존성 없어 객체가 안 바뀜 | `-MMD -MP` 헤더 의존성 추가 |
| keep-alive에서 요청이 섞임 | `req.consumed`를 지우지 않음 | `connection_feed()` 버퍼 정리, 최대 100회, 유휴 5초 |
| before 모드에서 초과 예약이 안 나옴 | 단일 accept 루프가 요청을 직렬 처리 | `worker` + `iomux`, 공유 listen fd, 이벤트당 accept 1건 |
| 시간이 흘러도 상태가 안 바뀜 | 실제 시계만 사용 | `flight_clock`에 sim/real 모드와 배속 추가 |

## 설계 결정

- 공유 상태는 `flight_store.c` 한 곳만 가집니다. HTTP/JSON은 모릅니다.
- 락 순서는 `flight.mu → g_store.mu`입니다. 파일 저장은 두 락을 모두 풀고 합니다.
- before 모드는 의도적 무락 경로입니다. 기본값은 after이고, 실행 중 `POST /api/admin/mode`로만 전환합니다.
- 시계는 UTC epoch 초입니다. `FLIGHT_TIME_MODE=sim`, `FLIGHT_SIM_SPEED`로 가상 시간을 흘립니다.
- API 응답은 스냅샷 복사본으로 만들고, 직렬화와 소켓 쓰기는 락 밖에서 합니다.

## 주요 엔드포인트

```text
GET  /api/flights
GET  /api/flights/{id}
POST /api/flights/{id}/reserve
GET  /api/flights/{id}/timeline
POST /api/admin/flights/{id}/status
POST /api/admin/mode
GET  /api/reservations
POST /api/reservations/{id}/cancel
GET  /api/ops/clock
POST /api/ops/flights/{id}/delay
```

## 검증

```sh
make test-flight          # 32스레드 좌석 경쟁
make integration-flight   # HTTP 100동시 + keep-alive + slow client
make integration-persist  # 재시작 후 예약/좌석/이력 유지
make test-tracking        # 시계 전이·지연 단위
make integration-tracking # 자동 출발/도착 통합
make tsan                 # before 데이터 레이스 1건, after 0건
```

실제 결과: after 모드에서 32명 중 1명만 성공, before 모드에서 32명 전부 성공
(잔여 -31), TSan은 before 구간에서만 레이스 1건을 검출했습니다.
