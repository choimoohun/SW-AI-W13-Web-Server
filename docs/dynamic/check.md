# 동적 처리 직접 확인

[예약 API](booking.md) · [프록시](proxy.md) · [전체 검증](../testing.md)

## 확인 순서

1. **창 A**에서 [README의 빠른 실행](../../README.md#빠른-실행)을 따라 웹 서버와 예약 백엔드를 시작한다. `8080`과 `8081`의 `Listening` 로그를 모두 확인하고 창 A를 열어 둔다.
2. 브라우저에서 예약하기 전에 **창 B**에서 아래 요청을 한 줄씩 실행한다. 새로 실행한 백엔드는 `1A`가 빈자리다. 이미 예약했다면 조회 결과가 `available: true`인 다른 좌석 번호를 두 POST에 같은 값으로 사용한다.

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

#### Windows와 Docker

1. Docker Linux 엔진 상태를 `docker info --format '{{.OSType}}'`로 확인한다. `linux`가 출력되면 **새 PowerShell 창 A**의 프로젝트 루트에서 장애 테스트 전용 프록시를 실행한다. 기존 `8080` 데모와 별도로 `18080`을 공개한다.

```powershell
docker compose run --build --rm --name week13-proxy-check --publish 127.0.0.1:18080:18080 dev sh -ec 'cmake -S . -B /tmp/proxy-check; cmake --build /tmp/proxy-check; exec /tmp/proxy-check/web_server 18080 public 0.0.0.0 --proxy 127.0.0.1 18081'
```

2. 창 A에 `Listening on 0.0.0.0:18080`이 출력되면, **창 B**에서 API와 정적 페이지에 차례로 요청한다. 백엔드가 비어 있으므로 API는 `502`, 정적 페이지는 `200`이 기대된다.

```powershell
curl.exe -i http://127.0.0.1:18080/api/seats
curl.exe -sS -o NUL -w "%{http_code}\n" http://127.0.0.1:18080/
```

3. **창 C**에서 같은 컨테이너 내부의 `18081`에 5초 지연 백엔드를 띄운다. `Slow backend ready`를 확인한 다음 창 B로 돌아간다.

```powershell
@'
use IO::Socket::INET;
$| = 1;
my $listener = IO::Socket::INET->new(
    LocalAddr => '127.0.0.1', LocalPort => 18081, Listen => 1, ReuseAddr => 1
) or die "listen: $!";
print "Slow backend ready\n";
my $client = $listener->accept() or die "accept: $!";
sleep 5;
close $client;
'@ | docker exec -i week13-proxy-check perl -
```

4. **창 B**에서 아래 명령을 실행한다. 약 3초 뒤 `504 Gateway Timeout`이 기대된다. 늦은 응답과 정적 요청이 겹치는지 보려면 API를 기다리는 동안 또 다른 창에서 2번의 정적 요청을 실행한다. 정적 응답이 먼저 완료돼야 한다.

```powershell
curl.exe -i -w "\nTotal: %{time_total}s\n" http://127.0.0.1:18080/api/seats
```

5. 지연 백엔드는 연결 한 번을 받고 끝난다. 창 A에서 `Ctrl+C`로 테스트용 프록시를 종료한다.

#### 로컬 POSIX 환경

[README의 로컬 빌드](../../README.md#빌드-및-검증)를 먼저 실행한다. **터미널 A**에서 `./build/web_server 18080 public 127.0.0.1 --proxy 127.0.0.1 18081`을 실행해 시작 로그를 확인한다. **터미널 B**에서 `curl -i http://127.0.0.1:18080/api/seats`를 실행하면 빈 백엔드 포트에서 `502`가 돌아온다. **터미널 C**에서 아래 지연 백엔드를 실행하고 준비 로그가 나오면 B에서 같은 요청을 다시 보내 `504`를 확인한다. 끝나면 A에서 `Ctrl+C`를 누른다.

```sh
python3 -c 'import socket, time; s = socket.create_server(("127.0.0.1", 18081)); print("Slow backend ready", flush=True); c, _ = s.accept(); time.sleep(5); c.close(); s.close()'
```
