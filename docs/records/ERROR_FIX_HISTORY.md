# 오류·수정 기록

구현·검증 중 실제로 드러난 문제와 수정입니다. “무엇을 어떻게 검증하는가”는 [../verify/VERIFICATION_MATRIX.md](../verify/VERIFICATION_MATRIX.md)에,
“왜 이 구조로 결정했는가”는 [../DESIGN.md](../DESIGN.md) §3에, 실행 출력은 [VERIFICATION_LOG.md](VERIFICATION_LOG.md)에 둔다.

| 단계 | 증상 | 원인 | 수정 | 재검증 |
|---|---|---|---|---|
| L0 기반 (STEP1~4) | 정적 파일 경로 탈출, 바이너리 손상, MIME 누락 | 경로 검증·청크 전송이 없었음 | `realpath` 경계 검사, 청크 전송, parser/static/response 분리 | `make test`, `make integration` |
| L1 동시성 (STEP5) | 단일 accept 루프가 100요청을 직렬 처리 → before 모드에서도 초과 예약이 안 나옴 | 연결당 처리가 블로킹 | `worker` + `iomux`, 공유 listen fd, 이벤트당 accept 1건 | `make review-cases` TC1 |
| L1 연결 | keep-alive에서 `req.consumed`를 안 지워 recv_buf에 다음 요청이 섞임 | 버퍼 정리 계약 없음 | `connection_feed()` 버퍼 정리, 최대 100회, 유휴 5초 | `make integration` 파이프라인 |
| L2 도메인 (STEP6) | 저장 파일이 0건으로 로드됨 | `split_fields()`가 구분자 뒤 포인터를 안 늘림 | 필드 파서 수정, `data/flights.txt` 영속 저장 | `make integration-persist` |
| L2 락 | `store_load()`의 `memset`이 초기화된 뮤텍스를 지움 | 락 초기화 순서 | 초기화 순서 변경 → 항공편별 락 + 메타 락 | `make test-flight` |
| L2 락 | 락을 든 상태에서 파일 쓰기 → 디스크 I/O가 예약을 막음 | I/O를 락 안에서 수행 | 스냅샷을 만든 뒤 락 밖에서 `rename()` | `make integration-flight` |
| 빌드 | `KE999`가 404 | `flight_t`에 뮤텍스 추가 후 객체가 안 바뀜 | `-MMD -MP` 헤더 의존성 추가 | `make test`, `make integration` |
| 빌드(Linux, 미해결) | Linux gcc에서 `tests/unit/test_static.c` 컴파일 실패 | `-Werror=format-truncation`: `snprintf` 버퍼 256B 경고(macOS clang은 없음) | 미수정 — 버퍼 크기 조정 필요 | Linux에서 `make test` |
| L3 게이트웨이 | 모든 라우트 404 | `trim()`이 앞 공백을 포인터만 옮겨 prefix가 `" /api/orders/"`가 됨 | `memmove`로 실제 버퍼를 당김 | `make integration-req` |
| L3 게이트웨이 | 느린 백엔드가 `/health`까지 막음 | blocking `recv()`가 워커 점유 | 전용 스레드 + 큐, `dup()` | `slow=2008ms / health=0ms` |
| L4 검증 | valgrind `epoll_pwait()` OOB | 이벤트 배열 64, 요청 최대 256 | 배열 256 + `min(max, cap)` | `make leak` 0 errors |
| 결제 | 게이트웨이 경유 충전 400 | 프록시가 `Idempotency-Key`를 안 넘김 | 중복 방지 키를 본문에도 포함 | `make integration-payments` |
| 결제 | 환불 요청 411 | 본문 없는 POST | `{}` 본문 전송 | `make integration-payments` |
| 결제 테스트 | `charge failed`, `payment down -> 200` | 수동으로 띄운 결제 서버가 8500 점유 | 시작 전 포트 검사 추가 후 재실행 | `payment integration: all passed` |
| 영속 테스트 | 예약 5개 항목 FAIL | 이전 실험 서버가 8080 점유 | 프로세스 정리 후 재실행 | `flight persistence: all passed` |
| 파이프라인 | 한 번에 볼낸 두 요청 중 첫 응답 본문이 빠지고 둘째 헤더가 이어짐 | `connection_feed()`가 응답이 남아 있어도 다음 요청 처리 → `file_fd` 덮어쓰기 | 응답이 남아 있으면 멈추고 `on_writable`에서 이어 처리 | `make integration` 파이프라인 2건 |
| 파이프라인 | 둘째 요청이 `Connection: close`면 응답 전에 연결이 닫힘 | `on_writable()`이 새 응답보다 `closing`을 먼저 확인 | 응답을 먼저 보내고 다음 차례에 닫기 | `make integration` 파이프라인 2건 |
| 헤더 | `Keep-Alive: max=100`이 `keepalive_max`와 무관 | 헤더 문자열 하드코딩 | `max` 제거, `timeout`은 유휴 정리와 같은 상수 | `curl -i` |
| L4 캐시·관측 | `X-Cache`만 있고 캐시 정책 헤더가 없음 | 정책 판단 근거 부족 | `Cache-Control`/`ETag`, `Date`/`Server`, 접근 로그 | `make integration-req` |
| TSan | `g_stop` 쓰기·읽기 data race | `volatile sig_atomic_t`는 스레드 간 동기화 보장 없음 | `atomic_int` | `make tsan` 후 `make integration` |
| 이식성 | Linux glibc에서 `usleep` 미선언 → `-Werror` 실패 | POSIX 호환성 | `nanosleep()` | Linux 빌드 |
| L5 백엔드 | 단일 오리진에 모든 도메인 → 독립 배포·자원 제한 불가 | 서비스 경계 없음 | `conf/backends.conf` 라우트로 redis·dbms·결제 분리 | `make integration-backends` |
