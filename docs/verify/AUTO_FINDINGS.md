# 검증에서 잡은 문제

실행 중 발견해서 고친 문제입니다. 설계 의견은 [DESIGN_REVIEW.md](../design_reviews/DESIGN_REVIEW.md)에 둡니다.

| 단계 | 증상 | 원인 | 수정 | 재검증 |
|---|---|---|---|---|
| 빌드 | `KE999`가 404 | `flight_t`에 뮤텍스 추가 후 객체가 안 바뀜 | `-MMD -MP` 추가 | `make test`, `make integration` |
| 게이트웨이 | 모든 라우트 404 | `trim()`이 앞 공백만 포인터로 넘김 | `memmove`로 버퍼 당김 | `make integration-req` |
| 프록시 | 느린 백엔드가 `/health`까지 막음 | blocking `recv()`가 워커 점유 | 전용 스레드 + 큐 | `slow=2008ms / health=0ms` |
| valgrind | `epoll_pwait()` OOB | 이벤트 배열 64, 요청 최대 256 | 배열 256 + clamp | `make leak` 0 errors |
| 결제 | 게이트웨이 경유 충전 400 | 프록시가 `Idempotency-Key`를 안 넘김 | 중복 방지 키를 본문에도 포함 | `make integration-payments` |
| 결제 | 환불 요청 411 | 본문 없는 POST | `{}` 본문 전송 | `make integration-payments` |
| 영속 테스트 | 예약 5개 항목 FAIL | 이전 실험 서버가 8080 점유 | 프로세스 정리 후 재실행 | `flight persistence: all passed` |