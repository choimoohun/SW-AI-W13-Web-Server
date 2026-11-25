# 요구사항 추적 (RTM)

요구사항이 실제 코드와 테스트로 추적되는지 보여 주는 표입니다(requirement traceability).
요구사항의 정의는 [REQUIREMENTS.md](../REQUIREMENTS.md)에, 실행 출력은 [VERIFICATION_LOG.md](../records/VERIFICATION_LOG.md)에 있다.

| 요구사항 | 푸는 문제 | 구현 | 검증 |
|---|---|---|---|
| FR-01 | 연결을 받을 진입점 | `src/tcpserver.c`, `src/main.c` | `make test`, `make integration` |
| FR-02 | 요청의 시작과 끝 | `src/http_parser.c` | `make test` |
| FR-03 | 응답 구조 | `src/http_response.c` | `make integration` |
| FR-04 | 경로 탈출과 MIME | `src/static_handler.c` | `make integration` |
| FR-05 | 요청마다 다른 결과 | `src/flight_api.c`, `src/flight_store.c` | `make integration-flight` |
| FR-06 | 느린 클라이언트와 동시 요청 | `src/iomux.c`, `src/connection.c`, `src/worker.c` | `make integration-req`, `make load-test-heavy` |
| FR-07 | 서비스 분리 | `src/proxy.c`, `conf/backends.conf` | `make integration-backends` |
| FR-08 | 클라이언트 대리 요청 | 범위 밖 | `OUT OF SCOPE` |
| NFR-01 | 메모리 안전 | 전체 소스 | `make asan`, `make leak` |
| NFR-02 | 종료 시 정리 | `src/main.c` | `make integration-req` |
| NFR-03 | 유휴 연결 정리 | `src/connection.c`, `src/worker.c` | `make integration-req` |
| NFR-04 | 요청 흔적 | `src/connection.c` | `make integration-req` |
| C-01 C11/POSIX | 언어·빌드 제약 | `Makefile`(`-std=c11 -Wall -Wextra -Werror`) | `make` |
| C-02 외부 라이브러리 금지 | 완성된 HTTP/이벤트 라이브러리 사용 금지 | `Makefile`(`LDLIBS ?= -pthread`), `src/*.c` | `make` |
| C-03 Linux/macOS 이식성 | OS별 I/O 감시 API 차이 | `src/iomux.c`(`__linux__`/`__APPLE__`/poll) | `tests/linux_check.sh` |
| C-04 Makefile 타깃 | 빌드·검증 명령 제공 | `Makefile` | `make test`, `make asan`, `make clean` |
| C-05 CLI/설정 주입 | 포트·루트·라우팅을 실행 시 주입 | `src/config.c`, `conf/*.conf` | `make integration-req` |

아직 연결이 약한 곳은 [DAILY_SCOPE.md](../agent/DAILY_SCOPE.md)에 적습니다.