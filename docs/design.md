# C 웹 서버 설계 목차

[과제 명세](spec.md) · [진행 상태](progress.md) · [실행 방법](../README.md)

## 현재 서버

- [요청 처리 흐름](server.md) · TCP 연결, HTTP 파싱과 응답, 정적 파일 서빙, FD 정리

## 1.5 동적 요청과 프록시

1. [좌석 조회·예약](dynamic/booking.md) · 백엔드의 상태, API 입출력과 예약 충돌
2. [리버스 프록시](dynamic/proxy.md) · 경로 라우팅, TCP 중계, 실패와 타임아웃
3. [직접 확인](dynamic/check.md) · 실행 명령, 예약 전후 요청, 브라우저와 장애 확인

프록시는 요청을 별도 예약 백엔드에 중계한다. 현재 작업의 검증 상태는 [진행 상태](progress.md)를 확인한다.

## 1.6 연결과 동시 처리

1. [연결 수락과 전달](concurrency/connections.md) · 커널 대기열과 애플리케이션 작업 큐
2. [워커 풀](concurrency/workers.md) · 동시성 모델, 제한된 작업 큐, FD 소유권
3. [I/O 기한](concurrency/timeouts.md) · 클라이언트 수신·송신과 백엔드 대기
4. [안전한 종료](concurrency/shutdown.md) · 종료 신호에서 워커와 FD 정리까지
5. [직접 확인](concurrency/check.md) · 동시 응답, 포화, 타임아웃, 종료와 부하

## 검증

- [단계별 검증](testing.md) · 각 기능의 합격 기준, 회귀 테스트, 자원 검사
- [검증 결과](results.md) · 실제 테스트와 부하 측정 기록

다음 세션은 [진행 상태](progress.md)에서 현재 위치를 보고, 변경할 기능의 설계 문서와 소스 진입점을 읽는다. 실행 절차와 기대값은 직접 확인 문서에, 실제 관찰값은 검증 결과에 남긴다.
