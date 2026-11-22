# STEP2 — HTTP 요청 파싱

## 목표
수신한 바이트를 HTTP 요청으로 해석한다.

## 작업 범위
- 요청 줄/헤더/본문 파싱
- 메서드/URI/버전 검증
- Host, Content-Length, Connection 처리(대소문자 무시)
- Content-Length 기반 본문 경계 판단
- 잘못된 요청/미지원 요청 오류 처리

## 요구사항 기준
- GET/POST, HTTP/1.0·1.1
- URI 2048, 헤더 8192, 본문 1MiB 상한
- HTTP/1.1 Host 필수

## 관련 모듈

| 파일 | 책임 |
|---|---|
| include/http_parser.h | 요청 구조체, 파싱 상태 |
| src/http_parser.c | 순수 파서(소켓 IO 없음) |
| src/connection.c | read 누적, 파서 호출 |

## 의존 관계

```text
connection.c → http_parser.c
```

http_parser는 버퍼+길이만 받고 소켓을 직접 읽지 않는다.

## 구현 기록
- http_parser_parse(buf, len, req): \r\n\r\n 탐색 → 요청 줄 분해 → 헤더 순회 → 본문 경계. 반환은 COMPLETE / NEED_MORE / ERROR(err_status).
- 오류 코드: 400(형식/Host 누락), 405(메서드), 411(POST CL 누락), 413(본문 초과), 414(URI 초과), 431(헤더 초과), 501(chunked), 505(버전).
- connection: read로 buf 누적, NEED_MORE면 더 읽고 COMPLETE/ERROR에서 분기. 입력 버퍼는 수정하지 않는다.

## 테스트 기록
- make test → test_parser all passed (오류 코드, short read, 대소문자 무시, 중복 CL 거부).
- curl/nc: GET / 200, 잘못된 요청 400, POST CL 누락 411, DELETE 405, Host 누락 400.

## 확인할 항목
- [x] 요청 줄 구분
- [x] GET/POST 구분
- [x] 버전 확인
- [x] 헤더 이름/값 구분
- [x] Content-Length 본문 수신 확인
- [x] 잘못된 형식/크기 초과 구분

## 관련 결정
- D3 (chunked 미지원 501)
