# STEP5 — 동시 요청 처리 결정

이 단계에서 확정한 결정과 트레이드오프입니다. 전체 목록은 [DECISIONS.md](../DECISIONS.md)에 있습니다.

| ID | 결정 사항 | 배경 및 사유 | 트레이드오프 |
|---|---|---|---|
| D5 | Envoy식 multi-reactor: 워커 스레드 N개, 각자 iomux(poll/epoll/kqueue) 이벤트 루프 (단일 프로세스) | shared-nothing 워커 + 코어 어피니티 원칙과, 항공편 예약 데모의 공유 상태(좌석) 요구를 동시에 만족. 스레드라 shm/좀비/재기동 코드 불필요, accept한 워커가 연결을 끝까지 소유해 fd 핸드오프 불필요 | 단일 루프+큐+스레드풀 안 대비 루프 코드가 워커별이지만 소유권 규칙이 단순해짐. 멀티프로세스 안 대비 크래시 격리는 없음. 어피니티는 Linux 전용(#ifdef), accept 분산은 공유 listen fd 기본안(macOS 이식성), SO_REUSEPORT/EPOLLEXCLUSIVE는 Linux 선택 최적화 |
| D9 | epoll(Linux)/kqueue(macOS) 백엔드 + poll 폴백 | 준비된 fd만 커널이 통지 → O(ready) 스캔. 같은 `iomux` 인터페이스라 워커 코드는 불변 | 플랫폼별 `#ifdef` 분기 증가 |
| D10 | keep-alive + 단일 워커 accept 1건 정책 | 이벤트 루프에서 요청 겹침을 보장하려면 accept를 여러 워커에 분산해야 함. 한 번에 다 accept하면 첫 워커가 독점해 경쟁이 사라짐 | accept 시스템콜 수 증가. 데모 규모에서는 허용 |
| D20 | 부하 테스트는 전용 도구 없이 curl 병렬 방식까지 허용 | `hey`/`wrk` 미설치 환경에서도 실행 가능해야 함 | 정교한 percentile 측정은 불가. RPS는 근사값 |
| D21 | iomux에 epoll(Linux)/kqueue(macOS) 백엔드 추가, poll은 폴백 (D9와 동일) | 준비된 fd만 커널이 통지 → O(ready) 스캔. 같은 인터페이스라 워커 코드는 불변 | 플랫폼 분기 `#ifdef` 증가 |
| D22 | 프록시를 전용 스레드 + 큐로 분리 | 느린 백엔드가 워커 이벤트 루프 전체를 멈춤. `dup()` fd로 소유권 이전 후 워커는 즉시 복귀 | 컨텍스트 복사 비용, 스레드 1개 추가 |
| D25 | Makefile에 `-MMD -MP` 헤더 의존성 추가 | `flight_t`에 뮤텍스 추가 후 헤더 포함 객체가 재컴파일되지 않아 구조체 레이아웃 불일치 버그 발생 | `.d` 파일 생성(.gitignore 처리) |
