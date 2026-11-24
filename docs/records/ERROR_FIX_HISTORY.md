# 오류를 만날 때마다 얹은 레이어 (STEP1 → STEP6)

구현 중 실제로 드러난 문제와 그 해결입니다. 현행 구조 설계는 [../DESIGN.md](../DESIGN.md)를 본다.

## 오류를 만날 때마다 레이어를 얹은 구조 (STEP1 → STEP6, FR-05~07)

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
| L4 캐시·관측 | `X-Cache`만 있고 `Cache-Control`/`ETag`가 없어 캐시 정책 판단 불가 | 캐시 헤더 추가, `Date`/`Server`, 접근 로그(`[wN]`, IP, 지연) |
| L4 검증 | valgrind가 `epoll_pwait(events)` OOB 검출(배열 64, 요청 max 256) | 이벤트 배열 256으로 확대, `min(max, cap)` 클램프 |
| 이식성 | Linux glibc에서 `usleep` 미선언 → `-Werror` 빌드 실패 | `nanosleep()`으로 교체 |
| L5 백엔드 | 단일 오리진에 모든 도메인을 두면 독립 배포·리소스 제한 불가 | `conf/backends.conf` 라우트로 mini-redis·mini-dbms 별도 프로세스 연결 |
