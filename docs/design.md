# C 웹 서버 설계 목차

[과제 명세](spec.md) · [진행 상태](progress.md) · [실행 방법](../README.md)

## 현재 서버

- [요청 처리 흐름](server.md) · TCP 연결, HTTP 파싱과 응답, 정적 파일 서빙, FD 정리

## 1.5 동적 요청과 프록시

1. [좌석 조회·예약](dynamic/booking.md) · 백엔드의 상태, API 입출력과 예약 충돌
2. [리버스 프록시](dynamic/proxy.md) · 경로 라우팅, TCP 중계, 실패와 타임아웃
3. [직접 확인](dynamic/check.md) · 실행 명령, 예약 전후 요청, 브라우저와 장애 확인

예매 화면이 HTML과 API 요청을 차례로 보내므로 프록시 모드는 [순차 연결 수락](concurrency/connections.md)을 사용한다. 현재 작업의 검증 상태는 [진행 상태](progress.md)를 확인한다.

## 1.6 연결과 동시 처리

1. [순차 연결 수락](concurrency/connections.md) · 한 요청이 끝나면 다음 연결 받기
2. [워커 풀](concurrency/workers.md) · 동시성 모델, 제한된 작업 큐, FD 소유권
3. [안전한 종료](concurrency/shutdown.md) · 종료 신호, I/O 기한, 소켓과 워커 정리

## 검증

- [단계별 검증](testing.md) · 각 기능의 합격 기준, 부하와 자원 검사

1.6 워커 풀과 종료 정책은 검토안이다. 각 단위는 개념 학습, 기대 동작 확인, 구현, 검증 순서로 진행한다.
