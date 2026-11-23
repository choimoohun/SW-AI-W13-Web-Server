# AI 질의 → 검증 → 채택

사람이 검토해서 정한 것은 [HUMAN_REVIEW.md](HUMAN_REVIEW.md),
실행 중 발견한 문제는 [../verify/AUTO_FINDINGS.md](../verify/AUTO_FINDINGS.md)에 있다.
이 문서는 **AI에게 질의하고, 실행으로 검증하고, 설계에 채택한 것**을 담는다.

| 질의 | 검증 | 결론 | 반영 |
|---|---|---|---|
| 동시성 모델로 멀티프로세스 + 멀티플렉싱이 맞는가 | 공유 좌석을 `fork`로 찢으면 워커마다 사본이 생겨 공유 메모리/IPC가 필요해짐 | 채택: 단일 프로세스 + 워커 스레드 N개 | `DECISIONS.md` D5 |
| `epoll`/`kqueue` 대신 `poll`이어도 되나 | 준비된 fd만 받는 `epoll`/`kqueue`가 연결 증가에 유리 | 채택: epoll(Linux)/kqueue(macOS) + poll 폴백 | D9, D21 |
| `select`가 `poll`보다 나은가 | `FD_SETSIZE` 제한, 매 호출 전체 스캔 | 기각 | `docs/notes/iomux.md` |
| HTTP 메시지 큐를 추가해야 하나 | 요구사항에 없음. 프록시 큐 하나로 충분 | 기각 | `study_log` 2026-11-23 |
| 프로세스 간 통신을 메시지 큐로 하나 | 별도 프로세스가 아니라 같은 프로그램 안에서 상태 공유 | 기각 | `docs/notes/multiplexing-rationale.md` |
| CGI/FastCGI, Next.js, SSG는 범위인가 | 프레임워크/별도 프로세스 도구라 C 직접 구현 목표와 거리 | 범위 밖 | `study_log` 2026-11-22 |
| 정적 파일 I/O를 진짜 비동기화해야 하나 | 로컬 디스크는 빠르고 병목은 공유 좌석 락 | 선택 실험으로만 | `docs/design_reviews/ARCHITECTURE_REVIEW.md` |
| "직렬화", "`send_all`", "청크 전송" 용어 | 직렬화=바이트열로 나열, `send_all`은 부분 쓰기 루프, 청크는 HTTP `Transfer-Encoding`과 구분 | 문서 용어 정리 | `HUMAN_REVIEW.md` |
| 이벤트당 accept를 여러 번 되나 | `EAGAIN`까지 반복하면 한 워커가 독점 → 동시성 데모 실패 | 채택: 이벤트당 1건 | D10, `VERIFICATION_LOG.md` 132행 |
| 프록시를 워커에서 직접 호출하면 | 느린 백엔드가 워커를 점유 → `/health`까지 막힘 | 채택: 전용 스레드 + 큐 + `dup()` | `AUTO_FINDINGS.md`, D18 |

## 반영 후 실행으로 확인한 것

- `make test`, `make integration`, `make integration-req` — 회귀 없음.
- 느린 백엔드 2065ms 동안 `/health`가 0ms 응답 → 프록시 비차단 확인.
- 100동시 예약 after `201 1 / 409 99`, before `201 4 / 409 96` → 동시성 재현.
- `make leak` — in use at exit 0 bytes, ERROR SUMMARY 0.
