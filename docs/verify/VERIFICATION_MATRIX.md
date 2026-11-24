# 검증 매트릭스

요구사항 하나가 어느 명령으로 검증되는지 연결하는 문서입니다.
이 문서는 현재 상태를 보여 주고, 실제 실행 증거는 `../records/VERIFICATION_LOG.md`에 둡니다.
여기에 PASS라고 적으려면 로그에 해당 실행 기록이 있어야 합니다.

상태:

- `PASS`: 실제로 실행해 통과한 기록이 있음
- `NOT RUN`: 아직 실행하지 않음
- `OUT OF SCOPE`: 이번 범위 밖으로 확정
- `SUPERSEDED`: 이후 다른 방식으로 대체됨

| 요구사항 | 검증 명령 | 기대 결과 | 상태 | 증거 |
|---|---|---|---|---|
| C-01 C11/POSIX | `make` | `-Wall -Wextra -Werror` 통과 | PASS | `../records/VERIFICATION_LOG.md` 빌드 항목 |
| C-02 외부 라이브러리 금지 | `grep` / Makefile 확인 | `libevent`, `libuv`, `curl` 링크 없음 | PASS | `Makefile`, `src/` |
| C-03 Linux/macOS 이식성 | `make`, Docker `tests/linux_check.sh` | 양쪽 빌드·회귀 통과 | PASS | Linux regression all passed |
| C-04 Makefile 타겟 | `make test`, `make asan`, `make clean` | 타겟 존재·실행 | PASS | `Makefile` |
| C-05 CLI/설정 | `./webserver -p -r -c`, `make integration-req` | 포트/루트/설정 적용 | PASS | `integration-req` |
| FR-01 TCP 서버 | `nc -zv 127.0.0.1 8080`, 재시작 | 포트 열림, 재바인딩 성공 | PASS | `STEP1`, `../records/VERIFICATION_LOG.md` |
| FR-02 HTTP 파싱 | `make test`, `make edge-cases` | 405/505/414/413, short read 조립 | PASS | `test_parser`, `edge-cases` |
| FR-03 HTTP 응답 | `curl -i`, `make integration` | 상태 라인·헤더·본문 정상 | PASS | `integration` |
| FR-04 정적 파일 | `make integration` | 200/403/404/500, PNG 무결성 | PASS | `test_static`, `integration` |
| FR-05 동적 요청 | `make integration-req` | `/health`, `/echo`, `/api/*` 동작 | PASS | 안 B 채택, CGI 미구현 |
| FR-06 동시성 | `make integration-flight`, `make load-test-heavy` | 100동시 처리, before/after 재현 | PASS | 동시 100·1000, Non-2xx 0 |
| FR-07 Reverse Proxy | `make integration-req`, `make integration-backends` | RR, XFF, 502/504 | PASS | `slow=2008ms / health=0ms` |
| FR-08 Forward Proxy | - | 클라이언트 대리 요청 | OUT OF SCOPE | 선택 과제로 확정 |
| NFR-01 메모리 안전 | `make asan`, `make leak` | ASan 0, valgrind in use 0 | PASS | `ERROR SUMMARY: 0` |
| NFR-02 시그널 | `kill -TERM <pid>`, `echo $?` | 종료 코드 0 | PASS | `integration` |
| NFR-03 타임아웃/keep-alive | `make integration`, 프록시 지연 시나리오 | 유휴 5초, 최대 100회, 504 구분 | PASS | 느린 백엔드 시나리오 |
| NFR-04 접근 로그 | `make integration-req` | `[wN]`, IP, 지연, 상태코드 | PASS | requirements integration |
| 도메인 영속 저장 | `make integration-persist` | 재시작 후 예약/좌석/이력 유지 | PASS | 6개 항목 all passed |
| 출발/도착 추적 | `make test-tracking`, `make integration-tracking` | 지연·자동 전이·이벤트 | PASS | tracking all passed |
| 별도 백엔드 연결 | `make integration-backends` | 리소스 제한, 독립 재시작 | PASS | 200/502 혼재 후 복구 |
| 결제 서비스 | `make integration-payments` | 충전·중복 방지·환불·재시작 복원 | PASS | payment integration all passed |
| 잘못된 요청/끊김 | `make edge-cases` | 405/505/414/413/502, 생존 | PASS | edge cases all passed |
| 뮤테이션 테스트 | `make mutation` | 두 mutant 모두 테스트가 실패 | PASS | `mutation tests: all killed` |
| Read/Write timeout 전용 시연 | - | 서버가 5초 후 연결 종료 | NOT RUN | 코드에는 적용, 별도 스크립트 없음 |
| 물리 링크 장애 | - | 케이블 단절/복구 | OUT OF SCOPE | 현재 단계 범위 밖 |

## 새 항목을 추가할 때

1. `docs/REQUIREMENTS.md`에 요구사항이 있으면 여기에 행을 추가합니다.
2. 검증 명령을 적고, 실행 전에는 `NOT RUN`으로 둡니다.
3. 실행 후 실제 출력 요약을 `../records/VERIFICATION_LOG.md`에 append하고 상태를 `PASS`/`FAIL`로 바꿉니다.
4. 실패를 지우지 않고 원인·수정·재검증을 같은 날 기록에 남깁니다.
5. 요구사항이 바뀌면 이 표와 `STEP 문서`, `DECISIONS.md`를 함께 갱신합니다.

---

## 검증에서 잡은 문제

실행 중 발견해서 고친 문제입니다. 설계 의견은 [../agent/AI_QA_LOG.md](../agent/AI_QA_LOG.md)에,
초기 검토 기록은 [../records/DESIGN_REVIEW_2026-11-21.md](../records/DESIGN_REVIEW_2026-11-21.md)에 둡니다.

| 단계 | 증상 | 원인 | 수정 | 재검증 |
|---|---|---|---|---|
| 빌드 | `KE999`가 404 | `flight_t`에 뮤텍스 추가 후 객체가 안 바뀜 | `-MMD -MP` 추가 | `make test`, `make integration` |
| 게이트웨이 | 모든 라우트 404 | `trim()`이 앞 공백만 포인터로 넘김 | `memmove`로 버퍼 당김 | `make integration-req` |
| 프록시 | 느린 백엔드가 `/health`까지 막음 | blocking `recv()`가 워커 점유 | 전용 스레드 + 큐 | `slow=2008ms / health=0ms` |
| valgrind | `epoll_pwait()` OOB | 이벤트 배열 64, 요청 최대 256 | 배열 256 + clamp | `make leak` 0 errors |
| 결제 | 게이트웨이 경유 충전 400 | 프록시가 `Idempotency-Key`를 안 넘김 | 중복 방지 키를 본문에도 포함 | `make integration-payments` |
| 결제 | 환불 요청 411 | 본문 없는 POST | `{}` 본문 전송 | `make integration-payments` |
| 영속 테스트 | 예약 5개 항목 FAIL | 이전 실험 서버가 8080 점유 | 프로세스 정리 후 재실행 | `flight persistence: all passed` |
| 파이프라인 | 한 번에 보낸 두 요청 중 첫 응답 본문이 빠지고 둘째 헤더가 이어짐 (PR #22 리뷰 R11, 16KB 초과 파일) | `connection_feed()`가 보낼 응답이 남아 있어도 다음 요청을 처리 → `file_fd` 덮어쓰기 | 응답이 남아 있으면 멈추고 `on_writable`에서 이어 처리 | `make integration` 파이프라인 2건 |
| 파이프라인 | 둘째 요청이 `Connection: close`면 응답을 보내기 전에 연결이 닫힘 | `on_writable()`이 새 응답 확인보다 `closing` 확인을 먼저 함 | 응답을 먼저 보내고 다음 차례에 닫도록 순서 변경 | `make integration` 파이프라인 2건 |
| 헤더 | `Keep-Alive: max=100`이 `keepalive_max` 설정과 무관하게 고정 (PR #22 리뷰 R12) | 헤더 문자열 하드코딩 | `max` 제거, `timeout`은 유휴 정리와 같은 상수 | `curl -i` → `Keep-Alive: timeout=5` |
| TSan | `on_signal`의 `g_stop` 쓰기와 워커 읽기가 data race | `volatile sig_atomic_t`는 스레드 간 동기화를 보장하지 않음 | `atomic_int` + `atomic_store`/`atomic_load` | `make tsan` 후 `make integration` → 경고 0 |
| 빌드(Linux, 미해결) | Linux gcc에서 `tests/unit/test_static.c` 컴파일 실패 | `-Werror=format-truncation`: `snprintf` 버퍼 256바이트 경고 (macOS clang은 경고 없음) | 아직 안 고침 — 버퍼 크기 조정 필요 | Linux에서 `make test` |
