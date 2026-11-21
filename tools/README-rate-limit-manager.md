# Hermes / Nous Rate Limit Manager

Hermes TUI의 `~/.hermes/logs/agent.log`를 분석하고, 사용자가 정한 일일/작업별 요청 예산과 로그에 기록된 Fair-share cooldown을 검사하는 로컬 도구입니다.

> 중요: 이 도구는 Hermes TUI 내부의 입력/API 요청을 가로채지 않습니다. `request` 명령으로 사전 검사를 한 다음, 사용자가 직접 Hermes TUI에 프롬프트를 입력하는 방식입니다. Nous의 실제 잔여 fair-share 용량을 조회할 수도 없으므로 제한을 완전히 예방하지는 못합니다.

## 설치

프로젝트에 다음처럼 배치합니다.

```text
your-project/
└── tools/
    └── rate_limit_manager.py
```

Python 표준 라이브러리만 사용합니다.

## 처음 설정

프로젝트 루트에서 실행합니다. 예시 예산 22회는 사용자가 정하는 내부 계획값이며 Nous의 공식 무료 한도가 아닙니다.

```bash
python3 tools/rate_limit_manager.py plan-create --budget 22
python3 tools/rate_limit_manager.py task-add --id tcp-server --name "TCP 서버 구현" --budget 5
python3 tools/rate_limit_manager.py task-add --id http-parser --name "HTTP 요청 파서 구현" --budget 6
python3 tools/rate_limit_manager.py task-add --id http-response --name "HTTP 응답 생성" --budget 4
python3 tools/rate_limit_manager.py task-add --id static-files --name "정적 파일 서빙" --budget 5
python3 tools/rate_limit_manager.py task-add --id tests --name "테스트 및 문서화" --budget 2
```

## 매 요청 전 검사

Hermes TUI에 새 프롬프트를 입력하기 전에 실행합니다.

```bash
python3 tools/rate_limit_manager.py request --task http-parser
```

- `ALLOW`: 로컬 일일 예산, 작업별 예산, 일시 중지 상태, 로그에 기록된 cooldown 검사를 통과했습니다. 이제 TUI에 요청을 입력합니다.
- `BLOCKED`: 요청 예산 초과, 수동 일시 중지 또는 관측된 cooldown 때문에 로컬 게이트가 차단했습니다.

`request`는 프롬프트 한 건을 보낼 계획으로 보고 예약량을 1 증가시킵니다. Hermes 내부 재시도 횟수나 실제 토큰 수를 측정하는 값은 아닙니다. 검사 후 실제 요청을 보내지 않았다면 예약량은 수동으로 보정해야 합니다(현재 버전에는 예약 취소 명령이 없습니다).

## 상태 및 로그

```bash
python3 tools/rate_limit_manager.py plan status
python3 tools/rate_limit_manager.py scan
python3 tools/rate_limit_manager.py report
python3 tools/rate_limit_manager.py watch
```

`watch`는 기본 10초 간격으로 로그를 확인합니다. `Ctrl+C`로 종료합니다.

## 일시 중지 / 재개

```bash
python3 tools/rate_limit_manager.py pause --reason "Fair-share 오류 반복"
python3 tools/rate_limit_manager.py resume
```

## 저장 위치

기본 SQLite 파일은 실행한 현재 디렉터리의 `.hermes-rate-limits.sqlite3`입니다. Hermes 설정이나 소스 파일은 수정하지 않습니다.

## 제한 사항

- Nous의 실시간 fair-share 잔여량/다음 허용 시각은 조회할 수 없습니다.
- `retry_after`는 해당 응답이 제안한 대기 시간이며, 대기 후 성공을 보장하지 않습니다.
- 예산은 실제 API 호출/토큰이 아니라 사용자가 승인한 프롬프트 예약 횟수입니다.
- TUI에 직접 입력하는 것을 기술적으로 막지 않습니다. 실제 자동 차단에는 Hermes 내부 요청 경로에 검증된 훅/래퍼를 연결해야 합니다.

## 예약 취소

`request` 검사에서 ALLOW를 받았지만 실제로 TUI에 요청을 보내지 않았다면 마지막 예약을 취소할 수 있습니다.

```bash
python3 tools/rate_limit_manager.py cancel --task http-parser
```

## 로그 읽기 방식

로그 파일의 마지막 읽은 바이트 위치를 SQLite에 기록합니다. 따라서 `watch`나 `request`가 실행될 때마다 전체 로그를 다시 읽지 않고 새로 추가된 부분만 읽습니다. 로그 파일이 교체/회전된 경우 처음부터 다시 읽습니다.
