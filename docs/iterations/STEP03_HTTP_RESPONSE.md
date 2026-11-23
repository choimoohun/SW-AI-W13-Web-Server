# HTTP 응답 생성

## STEP3 - 상태 줄·헤더·본문 직렬화와 전송

## 목표
처리 결과를 HTTP 응답으로 직렬화해 전송한다.

## 작업 범위
- 상태 줄/헤더/본문 구성
- Content-Type, Content-Length, Connection 처리
- 부분 전송 대응, 전송 오류 처리

## 응답 구조

```text
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 566
Connection: close

<본문>
```

## 관련 모듈

| 파일 | 책임 |
|---|---|
| include/http_response.h | 응답 인터페이스 |
| src/http_response.c | 직렬화, send_all, 청크 전송 |
| src/connection.c | 응답 호출, fd 정리 |

## 구현 기록

| 위치 | 동작 |
|---|---|
| `http_response_send_error(fd, status)` | 상태 줄+헤더, `Content-Length: 0` |
| `http_response_send_file(fd, mime, file_fd, size)` | 헤더(`Content-Length=size`) 후 16KB 청크 `read` → `send_all` 반복. 전체 파일을 메모리에 올리지 않음 |
| `send_all` | 부분 쓰기/EINTR 반복 처리 |
| 본문 처리 | `Content-Length`는 바이트 수. 본문은 포인터+길이로 다뤄 바이너리 보존 |

## 테스트 기록
- curl -i: 상태 줄/헤더/빈 줄/본문 순서 확인.
- /test.png → Content-Type image/png, Content-Length 149, 원본과 cmp 일치.

## 확인할 항목
- [x] 상태 줄 형식
- [x] 헤더-본문 사이 빈 줄
- [x] Content-Length = 본문 바이트 수
- [x] Content-Type 일치
- [x] 부분 전송 처리
- [x] 전송 실패 처리

## 관련 결정
- D4, D24, D26 — [STEP03 결정](../decisions/STEP03_HTTP_RESPONSE_DECISIONS.md)
