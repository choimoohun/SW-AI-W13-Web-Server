# 작업 인계와 현재 상태

이 문서는 오늘/현재 단위의 상태를 적는 곳입니다. 영구 규칙은 `docs/process/TEAM_PROCESS.md`, 설계는 `docs/`, 실행 증거는 `verify/`에 둡니다.

## 현재 단위

STEP10 결제 서비스 연결과 문서 구조 정리를 끝냈습니다. PR #18이 열려 있고, 브랜치는 `eungi-dev`입니다.

## 실제 결과

- `make test`, `make integration`, `make integration-req`, `make integration-backends`, `make integration-payments`, `make edge-cases`: PASS
- `make load-test-heavy`: 동시 100, 요청 1000, Non-2xx 0
- `make mutation`: 두 mutant 모두 killed
- `make leak`: valgrind in use 0 bytes, ERROR SUMMARY 0
- Linux `tests/linux_check.sh`: epoll 회귀 all passed

실제 출력은 `verify/VERIFICATION_LOG.md`에 있습니다.

## 다음 작업과 경계

1. 미결제 예약 TTL → 좌석 복원 (`HOLD` → `CONFIRMED` / `RELEASED`)
2. 프록시 큐 상한 + `429`/`503`으로 폭주 대응
3. 로그인/세션: 미니 레디스의 `session:<token>` 패턴 연결

아직 하지 않은 것:

- 인증/인가. 현재 `client`는 브라우저 로컬 값이라 위조 가능
- TLS/HTTP2. 실배포 시 앞단 게이트웨이에서 종료 예정
- Forward Proxy, CGI
- Read/Write timeout 전용 시연 스크립트
- 물리 네트워크 링크 장애

## 기록 방식

- 문제 → 설계 → 검증 순서로 남깁니다.
- 실행하지 않은 테스트는 `NOT RUN`, 범위 밖은 `OUT OF SCOPE`으로 적습니다.
- 실패와 수정, 재검증을 지우지 않습니다.
- 팀 합의가 필요한 항목은 `docs/process/TEAM_PROCESS.md`에 미정으로 둡니다.

## 로컬·Git 상태

```text
원격: origin git@github.com:choimoohun/SW-AI-W13-Web-Server.git
브랜치: eungi-dev
PR: #18 (base eungi-main)
커밋·푸시: 담당자 요청과 확인 후 진행
```

개인 브랜치에 푸시해도 팀 저장소가 바뀌지 않는다는 점을 워크플로 문서에 명시해 두었습니다.
