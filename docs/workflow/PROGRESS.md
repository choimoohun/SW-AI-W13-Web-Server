# 단계 진행 지도

STEP 문서는 단계별 상세 기록이고, 이 문서는 전체 흐름을 한 장으로 보여 줍니다.
각 행의 "근거" 링크가 실제 실행 기록입니다. 요약만 읽고 완료로 판단하지 않습니다.

| 단계 | 문제 | 결정/구현 | 근거 |
|---|---|---|---|
| 1. TCP 서버 | listen/accept 흐름과 종료 처리 | `tcpserver_listen()`, `SIGTERM` graceful shutdown | [STEP1](../iterations/STEP1_TCP_SERVER.md) |
| 2. HTTP 파싱 | 바이트 스트림에서 요청 경계 확정 | `http_parser`, `CRLF/CRLF`, `Content-Length`, `req.consumed` | [STEP2](../iterations/STEP2_HTTP_PARSER.md) |
| 3. HTTP 응답 | 부분 쓰기와 표준 헤더 | `http_response`, 상태 라인, `Date`/`Server` | [STEP3](../iterations/STEP3_HTTP_RESPONSE.md) |
| 4. 정적 파일 | 경로 탈출, MIME, 바이너리 손상 | `realpath` 경계 검사, 청크 전송, MIME | [STEP4](../iterations/STEP4_STATIC_FILE.md) |
| 5. 동시성 | 단일 accept 직렬화, keep-alive 버퍼 섞임 | 워커 풀 + `iomux`, before/after, 스냅샷 저장 | [STEP5](../iterations/STEP5_CONCURRENCY.md) |
| 6. 항공편 예약 | 좌석 경쟁, 영속 저장 | 항공편별 락, 임시 파일 → `rename`, 타임라인 | [STEP6](../iterations/STEP6_FLIGHT_DEMO.md) |
| 7. 도메인 완성 | 시계 전이, 지연, 재시작 후 상태 | `flight_clock`, `/api/ops/clock`, admin API | [STEP7](../iterations/STEP7_DOMAIN_COMPLETION.md) |
| 8. 요구사항 마무리 | 미결정 항목, 관측, 경계 케이스 | 설정 포맷/501/CGI 미채택 확정, 접근 로그, 405/505/414/413/502 | [STEP8](../iterations/STEP8_REQUIREMENTS_WRAPUP.md) |
| 9. 별도 백엔드 | 독립 배포·자원 제한·장애 격리 | `conf/backends.conf` 라우트, rewrite, RR, 전용 프록시 스레드 | [STEP9](../iterations/STEP9_BACKEND_SERVICES.md) |
| 10. 결제 서비스 | 결제는 예약과 정책·수명이 다름 | 별도 프로세스 8500, 중복 방지 키, 환불, 결제 화면 | [STEP10](../iterations/STEP10_PAYMENT_SERVICE.md) |

## 현재 검증 지점

- 단위/통합: `make test`, `make integration`, `make integration-flight`
- 경계: `make edge-cases`
- 백엔드/결제: `make integration-backends`, `make integration-payments`
- 동시: `make load-test`, `make load-test-heavy`
- 메모리: `make asan`, `make leak`
- Linux(epoll): `tests/linux_check.sh`

실제 출력은 [검증 로그](../../verify/VERIFICATION_LOG.md)에 있습니다.

## 아직 하지 않은 것

- 로그인/세션(다음 단계, `session:<token>`을 미니 레디스에 저장하는 방향)
- TLS/HTTP2 종료(실배포 시 앞단 게이트웨이에서 처리)
- Forward Proxy, CGI
- Read/Write timeout 전용 시연 스크립트
- 물리 네트워크 링크 장애(두 노트북/크로스 케이블)

범위 밖이라고 해서 빠진 것이 아니라, 순서를 정해 둔 것입니다.
