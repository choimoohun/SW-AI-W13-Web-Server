# VERIFICATION_LOG — 구현·검증 기록

이 문서는 오늘 구현한 내용과 실제 실행으로 검증한 결과를 구조화해 기록한다.
(원시 대화 전체가 아니라 발표·재현에 필요한 검증 근거 요약. 원시 대화는 Hermes 세션 히스토리에 있음.)
README.md의 "구현 마일스톤 및 진행 상태"와 짝을 이룬다.

## 범위

- 오늘 범위: 정적 파일 서빙 완성 (마일스톤 1~4).
  TCP 수락 → HTTP 요청 파싱 → 정적 파일 찾기 → HTTP 응답 전송(바이너리 무결성).
- 제외: Proxy, CGI, Keep-Alive, 동시성(FR-06), 부하 테스트 (이후 단계).

## 구현 완료 모듈 (마일스톤 1~4)

| 모듈 | 파일 | 책임 |
|---|---|---|
| tcpserver | src/tcpserver.c | getaddrinfo→socket→SO_REUSEADDR→bind→listen→accept |
| http_parser | src/http_parser.c | 순수 파서: 요청라인/헤더/본문, COMPLETE/NEED_MORE/ERROR |
| static_handler | src/static_handler.c | 경로 탈출 방어(realpath 경계), MIME, 404/403/500, "/"→index.html |
| http_response | src/http_response.c | 상태줄+헤더 직렬화, send_all, 16KB 청크 전송 |
| connection | src/connection.c | read 누적→파싱→정적파일→응답→단일 close, 5초 타임아웃 |
| main | src/main.c | 시그널 정책(SIGPIPE 무시, sigaction SA_RESTART 끔), accept 루프, doc_root |

## 검증 결과 (실제 실행)

### 빌드
- `make` → exit 0. `-std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L` 통과.

### 단위 테스트 (`make test`)
- test_parser: all passed (요청라인/헤더/본문, 400/405/411/413/414/501/505, short read, 대소문자 무시, 중복 CL 거부).
- test_static: all passed (경로 탈출 403, 퍼센트 인코딩 탈출 403, MIME, 404, "/a..b"는 정상 이름).

### 기능(통합) 테스트 (`make integration`)
- GET / → 200 text/html + index.html
- GET /index.html, /style.css, /sample.txt → 200 (MIME 매핑)
- GET /nope → 404
- GET /../etc/passwd, /%2e%2e/etc/passwd → 403 (탈출 차단)
- POST /(본문) → 405
- GET /test.png → 원본 바이트 일치(cmp), image/png, Content-Length 149
- 10개 항목전부 PASS.

### 메모리 검사 (`make asan`, macOS)
- ASAN+UBSan 빌드 후 기능 시나리오 실행 → 메모리 오류/UB 리포트 없음(halt_on_error=1, 중단 없음).
- graceful shutdown: SIGTERM → exit 0.
- **플랫폼 제약:** macOS ASAN은 메모리 오류·UBSan만 지원하고 누수 검사(LeakSanitizer)는 Linux 전용.

### 누수 검사 (Linux Valgrind, Docker) — 확정
- Docker(debian bookworm, valgrind 3.19) 컨테이너에서 빌드 → `valgrind --leak-check=full --show-leak-kinds=all` 실행.
- 정적 파일 시나리오(GET /, 404, 403, PNG, POST 405) 실행 후 SIGTERM 종료.
- HEAP SUMMARY: in use at exit **0 bytes in 0 blocks**. total heap usage: 1 allocs, 1 frees, 64 bytes.
- "All heap blocks were freed -- no leaks are possible". ERROR SUMMARY: **0 errors**. valgrind exit 0.
- → **누수 0 bytes, 메모리 오류 0건 확정 (NFR-01 충족).**

## 테스트 실행 방법

```sh
make            # 빌드
make test       # 단위 테스트 (파서, 경로보안/MIME)
make integration# 기능 테스트 (서버 기동 후 curl 검증, 자동 종료)
make asan       # ASAN+UBSan 빌드 (macOS=메모리오류 검사)
# 누수 0 확정 (Linux/Docker):
open -a Docker  # Docker Desktop 데몬 실행 후
docker compose run --rm dev sh -c "cd /workspace && make && valgrind --leak-check=full ./webserver ..."
```

## 남은 항목

- 마일스톤 5: 누수 0 bytes 확정 완료(Linux valgrind). 부하 테스트(wrk/hey)는 동시성 구현 후.
- 동시성(FR-06) 모델 결정: 현재 단일 스레드 블로킹. thread pool vs poll 확정 → DECISIONS.md 기록.
- 부하 테스트는 동시성 구현 후 meaningful.
- 환경: 프로젝트가 iCloud 동기화 Desktop 안 → 파일 되돌림/placeholder 위험. ~/dev 등 밖으로 이동 권장.
- GitHub push / Milestone 구성(선택).

## 확정/제안 결정 (DECISIONS.md 기록 대상)

- 정적 경로에 POST → 405 (정적 서빙은 GET 의미론). [구현 반영, 확정 기록 권장]
- Transfer-Encoding: chunked → 501 (미지원). [구현 반영]
- Keep-Alive 미지원: Connection: close, 요청 1건마다 닫기. [구현 반영]
- 동시성 모델: 미결정 (thread pool 권장 vs poll). [확정 필요]

## 빌드 이슈 수정 기록

- Makefile 링크 규칙에 $(CFLAGS) 누락 → ASAN 빌드 시 런타임 미링크(undefined ___asan_*). 링크에 $(CFLAGS) 포함하도록 수정.
- test_static.c의 mkdtemp가 macOS에서 미선언 → mkdir+pid 기반 임시 디렉터리로 포터블하게 교체.
- integration 스크립트 고정 sleep 1 → ASAN 빌드 느린 시작 대응 위해 준비-대기 폴링으로 교체.
- Linux gcc에서 realpath 미선언(_POSIX_C_SOURCE=200809L만으로는 glibc XOPEN 확장 미노출) →
  피처 매크로를 _XOPEN_SOURCE=700으로 교체(Makefile·CMakeLists·compile_commands.json). macOS/Linux 모두 빌드 통과.
- Docker 컨테이너 빌드 시 호스트 macOS .o가 복사돼 링크 실패(file format not recognized) → 복사본에서 make clean 후 빌드.
