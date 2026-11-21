# DECISIONS.md — 결정 기록

구현하면서 확정한 결정만 기록한다. 단순 제안이나 미확정 항목은 목록에 넣지 않는다.
결정마다 이유와 영향을 같이 적어, 나중에 "왜 이렇게 했는지"를 코드와 문서에서 바로 찾을 수 있게 한다.

## 결정 목록

| ID | 결정 | 이유 | 관련 |
|---|---|---|---|
| D1 | 정적 파일 범위는 단일 스레드 블로킹으로 구현 | 오늘 범위 완주 우선. 동시성 모델 확정 후 분기만 교체 | STEP1, main/connection |
| D2 | 정적 경로에 POST 요청은 405 | 정적 서빙은 GET 의미론 | STEP4, connection |
| D3 | Transfer-Encoding: chunked 미지원, 501 | 본문은 Content-Length로만 판단 | STEP2, http_parser |
| D4 | 연결 재사용(keep-alive) 미구현, 매 응답 Connection: close로 종료 | 범위 단순화(HTTP/1.1 허용 방식) | STEP3, connection/http_response |
| D5 | 피처 매크로 _XOPEN_SOURCE=700 | realpath가 glibc XOPEN 확장 필요, Linux/macOS 이식성 | Makefile/CMakeLists |
| D6 | 경로 탈출은 컴포넌트 단위 .. 거부 + realpath 경계 검사 | 문자열 검사만으론 심볼릭링크·인코딩 우회를 못 막음 | STEP4, static_handler |

## 상세

### D1. 동시성 — 현재 단일 스레드 블로킹
- 상태: 확정(임시). FR-06 최종 모델(thread pool vs poll)은 미확정.
- 결정: 정적 파일 범위에서는 accept 후 connection_handle을 블로킹으로 호출한다.
- 이유: 정적 서빙을 먼저 완성·검증하기 위해. connection을 "연결 하나를 처리하는 자기완결 함수"로 두면, 동시성 모델을 정한 뒤 accept 루프의 분기만 교체하면 된다. 핸들러 자체는 재사용 가능하다.
- 영향: main.c accept 루프, connection.c.
- 비고: 최종 모델 확정 시 이 항목을 갱신한다. poll 안을 택하면 핸들러를 상태 머신으로 바꿔야 하므로, thread pool 안이 현재 구조를 보존한다.

### D2. 정적 경로 POST → 405
- 상태: 확정.
- 결정: 파싱은 GET/POST 모두 지원하되, 정적 파일 핸들러는 GET만 처리하고 POST는 405로 거절한다.
- 이유: 정적 서빙은 GET 의미론이다. POST 본문 처리는 동적 핸들러/CGI 단계에서 다룬다.
- 영향: connection.c 분기.

### D3. chunked 미지원 → 501
- 상태: 확정.
- 결정: Transfer-Encoding 헤더가 있으면 501로 거절한다. 본문 길이는 Content-Length로만 판단한다.
- 이유: chunked 파싱은 범위를 크게 늘린다. 이번 범위에서는 Content-Length 기반만 지원한다.
- 영향: http_parser.c.

### D4. 연결 재사용(keep-alive) 미구현
- 상태: 확정.
- 결정: 한 연결에서 여러 요청을 처리하는 재사용은 구현하지 않는다. 대신 매 응답에 Connection: close를 명시하고 요청 1건 처리 후 연결을 닫는다.
- 이유: HTTP/1.1 기본은 keep-alive지만, 서버가 응답에 Connection: close를 보내면 그 연결을 닫아도 된다(RFC 9110 허용). 프로토콜 위반이 아니라 서버가 persistence를 opt-out하는 합법적 방식이다. close를 명시하지 않으면 클라이언트가 재사용을 시도하다 오류를 볼 수 있으므로 반드시 명시한다. 연결 재사용 상태 관리(파이프라이닝, 유휴 타임아웃, 연결당 최대 요청 수)는 뒤로 미루고 정적 서빙에 집중한다. Keep-Alive는 요구사항에서 Should(권장)이라 MVP 필수는 아니다.
- 영향: connection.c, http_response.c.
- 비고: 요청마다 TCP 연결을 새로 맺는 오버헤드가 있다. 재사용은 향후 Should 항목으로 구현 가능.

### D5. 피처 매크로 _XOPEN_SOURCE=700
- 상태: 확정.
- 결정: _POSIX_C_SOURCE=200809L 대신 _XOPEN_SOURCE=700을 쓴다.
- 이유: Linux glibc에서 realpath는 XOPEN 확장이라 _POSIX_C_SOURCE만으로는 선언이 노출되지 않는다(빌드 실패). _XOPEN_SOURCE=700은 POSIX.1-2008을 포함하면서 realpath 등을 노출해 macOS/Linux 양쪽에서 빌드된다.
- 영향: Makefile, CMakeLists.txt, compile_commands.json.

### D6. 경로 탈출 방어 방식
- 상태: 확정.
- 결정: query 제거 → 퍼센트 디코딩 → 컴포넌트 단위 ".." 거부 → doc_root 결합 → realpath → 경계 포함 검사(resolved==root 또는 root+"/" 시작) 순으로 검증한다.
- 이유: 부분 문자열 검사만으로는 심볼릭링크나 인코딩 우회, "/www-evil" 같은 prefix 함정을 못 막는다. realpath로 실제 경로를 풀고 디렉터리 경계까지 확인해야 안전하다.
- 영향: static_handler.c.
- 비고: realpath와 open 사이 TOCTOU 경쟁 조건은 이번 규모에서 허용한다. 더 강한 방어가 필요하면 openat 기반으로 구성요소별로 연다.
