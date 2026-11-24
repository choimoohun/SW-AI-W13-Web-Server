# 동적 처리 직접 확인

[예약 API](booking.md) · [프록시](proxy.md) · [전체 검증](../testing.md)

## 확인 순서

1. [README](../../README.md)의 Docker 실행 명령으로 예약 백엔드와 C 웹 서버를 함께 실행한다.
2. PowerShell에서 아래 요청을 차례로 실행한다. 새로 실행한 백엔드는 `1A`가 빈자리다.

```powershell
curl.exe -i http://127.0.0.1:8080/api/seats
'{"trainId":"KTX101","seatId":"1A"}' | curl.exe -i -H "Content-Type: application/json" --data-binary '@-' http://127.0.0.1:8080/api/reservations
curl.exe -i http://127.0.0.1:8080/api/seats
'{"trainId":"KTX101","seatId":"1A"}' | curl.exe -i -H "Content-Type: application/json" --data-binary '@-' http://127.0.0.1:8080/api/reservations
```

| 요청 순서 | 기대 결과 |
| --- | --- |
| 최초 조회 | `200`, `1A.available`이 `true` |
| 최초 예약 | `201`, `status`가 `reserved` |
| 재조회 | `200`, `1A.available`이 `false` |
| 같은 좌석 재예약 | `409`, `error`가 `seat_already_reserved` |

브라우저에서 [예매 화면](http://127.0.0.1:8080/)을 열고 다른 빈 좌석을 예약한다. 좌석이 비활성화되고, 새로고침해도 같은 상태가 조회되는지 확인한다. 별도의 탭 두 개에서 같은 빈 좌석을 미리 선택해 둔 뒤 **차례로** 예약하면 두 번째 화면에 충돌 안내가 표시된다. 백엔드를 재시작하면 실습용 좌석 상태가 초기화되는지 확인한다.

## 오류·중계 확인

| 입력 또는 환경 | 기대 결과 |
| --- | --- |
| 잘못된 JSON이나 필수 필드 누락 | `400`, `invalid_reservation` |
| 형식이 맞는 미등록 좌석 `9Z` | `404`, `seat_not_found` |
| 예약 본문을 `text/plain`으로 전송 | `415`, `unsupported_media_type` |
| 백엔드 주소를 연결받는 프로세스가 없는 로컬 포트로 설정 | `502 Bad Gateway` |
| 연결을 받은 테스트 백엔드가 3초 안에 응답을 완료하지 않음 | `504 Gateway Timeout` |

프록시 장애 응답을 확인한 뒤 `GET /`로 정적 파일이 계속 제공되는지 확인한다. 테스트 결과는 [진행 상태](../progress.md)에 실제 관찰값으로 남긴다.

### 프록시 장애 재현

로컬 POSIX 환경에서 기본 데모를 종료한 뒤, 다음 명령으로 프록시만 실행한다. 포트 `18081`이 비어 있으면 아래 API 요청에 `502`가 돌아온다.

```sh
./build/web_server 8080 public 127.0.0.1 --proxy 127.0.0.1 18081
```

별도 POSIX 터미널에서 좌석 조회를 보낸다.

```sh
curl -i http://127.0.0.1:8080/api/seats
```

이제 Python 3가 있는 별도 터미널에서 한 연결을 받고 5초간 응답하지 않는 테스트 백엔드를 실행한다. 프록시는 같은 조회에 약 3초 후 `504`를 응답한다.

```sh
python3 -c 'import socket, time; s = socket.create_server(("127.0.0.1", 18081)); c, _ = s.accept(); time.sleep(5); c.close(); s.close()'
```
