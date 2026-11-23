# STEP4 — 정적 파일 서빙

## 목표
요청 URL에 대응하는 doc_root 하위 파일을 찾아 응답한다.

## 작업 범위
- doc_root 기준 경로 처리, "/" → index.html
- 파일 존재/접근 확인
- 경로 탈출 차단
- 확장자별 MIME
- 404/403/500 구분

## 관련 모듈

| 파일 | 책임 |
|---|---|
| include/static_handler.h | 파일 응답 descriptor |
| src/static_handler.c | 경로 검증, MIME, open |
| src/http_response.c | 파일 내용 전송 |

## 구현 기록
- static_handler_resolve(uri, len, doc_root): query 제거 → 퍼센트 디코딩 → 컴포넌트 단위 ".." 거부(403) → doc_root 결합 → realpath → 경계 포함 검사(밖이면 403) → stat/open → errno 매핑(ENOENT 404, EACCES 403, 기타 500).
- "/"·디렉터리는 index.html을 시도. MIME은 확장자 대소문자 무시 매핑.
- {status, mime, file_fd, size} 반환. file_fd 닫기는 connection이 맡는다.

## 테스트 기록
- make test → test_static all passed (탈출 403, 인코딩 탈출 403, MIME, 404, "/a..b"는 정상 이름).
- curl: / 200 text/html, /nope 404, /../etc/passwd 403, /%2e%2e/etc/passwd 403, /test.png 바이너리 일치.

## 확인할 항목
- [x] "/" 기본 문서
- [x] 존재 파일 내용 반환
- [x] 없는 파일 404
- [x] doc_root 밖 접근 차단
- [x] 확장자별 Content-Type
- [x] 바이너리 원본 일치

## 관련 결정
- D2, D17 — [STEP04 결정](../decisions/STEP04_STATIC_FILE_DECISIONS.md)
