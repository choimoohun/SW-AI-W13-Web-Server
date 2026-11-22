# Hermes Rate Limit Manager (Nous · Qwen · All Providers)

Hermes TUI의 로그(`~/.hermes/logs/agent.log`)에서 모든 프로바이더(`nous`, `qwen`, `openrouter` 등)의 429/재시도 이벤트를 자동 감지하고, 오늘 할 목표를 한 줄로 입력하면 작업 단계와 안전 예산을 자동으로 나누어 주는 로컬 도우미입니다.

> 한계 및 주의사항
> - Hermes TUI 내부 API 호출을 강제로 가로채지 않습니다.
> - 에이전트는 질문 1번에 내부적으로 여러 번 API를 호출할 수 있으므로, 한 작업 단계가 끝날 때마다 Hermes 채팅창에 `/new`를 입력해 대화를 새로 여는 것이 리밋 방지에 가장 효과적입니다.

---

## 설치 및 자동 실행 연결 (최초 1회)

스크립트 위치: `~/.hermes/tools/rate_limit_manager.py` (Python 3.10 이상, 추가 패키지 설치 불필요)

터미널에서 `hermes` 실행 시 자동으로 오늘 계획 확인 및 로그 스캔이 돌도록 쉘 설정 파일 맨 아래에 추가합니다.

- macOS (`~/.zshrc`):
  ```bash
  hermes() {
    python3 "$HOME/.hermes/tools/rate_limit_manager.py" auto && command hermes "$@"
  }
  ```
- Linux / WSL2 (`~/.bashrc`):
  ```bash
  hermes() {
    python3 "$HOME/.hermes/tools/rate_limit_manager.py" auto && command hermes "$@"
  }
  ```

---

## 평소 사용법 (명령어 외울 필요 없음)

1. 터미널에 그냥 `hermes`를 입력합니다.
2. 그날 처음 실행이면 "오늘 하고 싶은 작업을 편하게 한 줄로 입력하세요"가 뜹니다.
   - 예: `동시성 STEP.md 완성하고 항공편 예약 및 출발/도착 시간 추적 서버 만들기`
   - 안전 모드(`[1] 보수적 / [2] 균형 / [3] 넉넉함`)를 고르거나 엔터를 누르면 `t1 ~ t4` 단계와 예산이 자동 배분되고 즉시 Hermes TUI가 켜집니다.
3. 같은 날 다시 `hermes`를 켜면 묻지 않고 오늘 작업 현황과 현재 쿨다운 여부(Nous/Qwen 등)만 보여주고 바로 실행됩니다.

---

## 자주 쓰는 보조 명령어

```bash
# 오늘 목표를 한 줄로 다시 입력해서 작업/예산 새로 나누기
python3 ~/.hermes/tools/rate_limit_manager.py setup

# 현재 작업 현황 및 프로바이더별 쿨다운 상태 확인
python3 ~/.hermes/tools/rate_limit_manager.py plan status

# 로그에서 감지된 429/rate-limit 통계 보기 (Nous, Qwen 등)
python3 ~/.hermes/tools/rate_limit_manager.py scan
python3 ~/.hermes/tools/rate_limit_manager.py report

# 실제 agent.log에 429/qwen/nous 로그가 어떻게 찍히고 있는지 원본 줄 확인
python3 ~/.hermes/tools/rate_limit_manager.py inspect

# 옆 터미널 창에 켜두고 실시간 429 감시하기 (종료: Ctrl+C)
python3 ~/.hermes/tools/rate_limit_manager.py watch

# (선택) 질문 직전 수동 게이트 검사 (기본 작업 또는 t1~t4 지정)
python3 ~/.hermes/tools/rate_limit_manager.py request --task t1
```

## 데이터 저장 위치
- 기본 SQLite DB: `~/.hermes/rate-limit-manager.sqlite3` (실행 폴더가 바뀌어도 기록 유지)
- 다른 DB를 쓰려면 `--db <경로>` 옵션을 추가하세요.