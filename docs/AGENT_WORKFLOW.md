# 에이전트 작업 안내

이 문서는 AI가 이 저장소에서 일할 때의 읽기 순서와 실행 경계입니다.
팀 협업 규칙은 [process/TEAM_PROCESS.md](./TEAM_PROCESS.md)에 있고,
사람이 판단한 흔적은 [COLLABORATION.md](./COLLABORATION.md)에 있습니다.

## 작업 전에 읽을 순서

1. [README.md](../README.md): 프로젝트 입구와 현재 상태
2. [WORKLOG.md](WORKLOG.md): 현재 단위, 실제 결과, 다음 작업
3. [why_webserver.md](./why_webserver.md): 프로젝트가 푸는 중심 문제
4. [REQUIREMENTS.md](./REQUIREMENTS.md): 요구사항과 수용 기준
5. [DESIGN.md](./DESIGN.md), [DECISIONS.md](./DECISIONS.md): 구조와 선택 근거
6. [TEST_MATRIX.md](verify/TEST_MATRIX.md), [VERIFICATION_LOG.md](verify/VERIFICATION_LOG.md): 검증 상태와 실행 증거

문서와 실제 코드가 다르면 차이를 먼저 보고합니다.

## 실행 경계

- 요청 없는 커밋·푸시·PR 생성은 하지 않습니다.
- 미정 정책을 확정하지 않습니다. 판단이 필요하면 사람에게 묻습니다.
- 실행하지 않은 테스트를 PASS라고 적지 않습니다.
- 실패와 수정, 재검증을 지우지 않습니다.
- AI 리뷰 제안도 로컬 명령으로 확인한 뒤 반영합니다.

## CodeRabbit 사용

CodeRabbit은 보조 리뷰어입니다. PR 코멘트에서 사용하는 명령:

```text
@coderabbitai review          # 새 커밋 리뷰
@coderabbitai full review     # 전체 파일 다시 리뷰
@coderabbitai pause / resume  # 자동 리뷰 중지/재개
@coderabbitai resolve         # 코멘트 해결 표시
@coderabbitai <질문>           # 자연어 질문
```

명령은 한 코멘트에 하나씩 적습니다. CodeRabbit이 제안한 변경도 `make test` 같은
실제 명령으로 확인합니다.

## 문서와 증거

- 요구사항·설계·결정은 `docs/`가 기준입니다.
- 실제 실행 증거는 `verify/`에만 둡니다.
- 에이전트 임시 맥락은 저장소에 남기지 않고, 판단만 `COLLABORATION.md`에 남깁니다.
