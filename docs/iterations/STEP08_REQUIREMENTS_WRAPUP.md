# STEP8 — 요구사항 마무리와 관측

요구사항 문서에 남아 있던 `[결정 필요]` 항목을 실제 구현대로 확정하고,
헤더·로그·CLI·경계 케이스를 정리한 단계입니다.

## 확정한 결정

| 항목 | 결정 |
|---|---|
| 설정 파일 포맷 | `key = value`, `conf/tcpserver.conf`, `conf/backends.conf` |
| `Transfer-Encoding: chunked` | 미지원, `501 Not Implemented` |
| FR-05 동적 요청 | 안 B(내장 핸들러 + 백엔드 프록시). CGI는 구현하지 않음 |
| FR-06 동시성 | 워커 풀 + `iomux`. `fork` 멀티프로세스는 사용하지 않음 |
| 라우팅 기준 | Path prefix. `route = prefix|backend...|rewrite`로 경로 수정 |

## 관측과 표준 헤더

- `Date`, `Server: jungle-c/1.0` 헤더를 모든 응답에 포함합니다.
- 접근 로그 형식: `[시간] [wN] [client_ip] "METHOD URI" STATUS BYTES ms`.
- `GET /health`, `POST /echo`를 내장 엔드포인트로 제공합니다.
- 정적 응답에 `Cache-Control`, 약한 `ETag`를 추가했습니다.

## 경계 케이스

`tests/integration/edge_cases.sh`가 실제로 확인합니다.

```text
지원하지 않는 메서드  → 405
잘못된 HTTP 버전      → 505
3000자 URI            → 414
1MB 초과 본문         → 413
백엔드 미기동         → 502
불완전 요청 후 종료   → 서버 생존, 다음 /health 200
```

`curl --max-time`은 클라이언트 측 제한입니다. 서버 타임아웃 자체를 보려면
서버가 직접 닫는 연결을 관찰해야 하며, 느린 요청/응답 시나리오는 아직 별도
검증 스크립트가 없습니다.

## 검증

```sh
make integration-req   # 헤더/health/echo/프록시 RR/XFF/502/로그/캐시 헤더
make edge-cases        # 위 경계 케이스 전부 PASS
make asan         # macOS ASan/UBSan
make leak              # Linux valgrind, in use 0 bytes
make load-test         # 동시 20, 200요청
make load-test-heavy   # 동시 100, 1000요청
```

동시 100·1000요청 결과: 1000개 전부 200, Non-2xx 0, 접근 로그에서 `w0~w3` 확인.
