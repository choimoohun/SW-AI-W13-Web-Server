# 리버스 프록시 테스트 케이스

[design.md](./design.md)의 1차 구현 범위를 확인하는 테스트입니다.

## 단위·루프백 테스트

`tests/test_proxy.c`에 작성하고 CTest에 `test_proxy`로 등록합니다. 외부 서버 없이 `127.0.0.1`의 빈 포트에 모의 백엔드 스레드를 띄우고, 클라이언트 쪽은 `socketpair()`로 받습니다. 시간 제한은 200ms(P11, P12는 2000ms)입니다.

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| P01 | `/api/=127.0.0.1:8001,localhost:8002`, `/=[::1]:8003` 등록 | 접두사 `/api`, 백엔드 2개. `/`는 그대로, IPv6는 `AF_INET6` |
| P02 | `/` 미시작, `=` 없음, 중복 접두사, 공백, 포트 없음·0·65536·문자, 빈 호스트, 끝 쉼표, 닫히지 않은 `[`, 빈 목록, 괄호 없는 IPv6(`::1`, `::1:80`) | 모두 -1, `errbuf`에 원인, 라우트 수 그대로 |
| P03 | 백엔드 17개, 라우트 17번째 | -1 |
| P04 | `/api`, `/api/v2` 등록 후 매칭 | `/api`, `/api/`, `/api/a`, `/api?x=1`, `/api/v20` → `/api`. `/api/v2/x?y` → `/api/v2`. `/apiv2`, `/` → `NULL` |
| P05 | `/` 추가 | `/apiv2` → `/`, `/api/v2/x`는 여전히 `/api/v2` |
| P06 | 백엔드 4개 라우트에 스레드 8개가 100번씩 `proxy_route_next()` | 백엔드마다 정확히 200번 |
| P07 | hop-by-hop 헤더가 섞인 GET 헤더 변환 | `HTTP/1.0` 요청 줄, URI·쿼리 유지, `Connection`·`Keep-Alive`·`Proxy-Connection`·`TE`·`Upgrade`·`Expect`·`X-Secret`(Connection 토큰) 제거, `Authorization`·`Accept` 유지, `Host: 127.0.0.1:8001`, `Connection: close` |
| P08 | 중복 `Content-Length` POST, 빈 POST, `Transfer-Encoding : chunked`(이름 뒤 공백), 헤더 값의 단독 `\r`, 작은 버퍼, URI 안의 단독 `\r` | `Content-Length` 하나, IPv6 Host는 `[::1]:8002`, 빈 POST는 `Content-Length: 0`, 나머지 넷은 -1 |
| P09 | 정상 응답 헤더 200/404/204/304 | 상태 코드 반환, hop-by-hop 제거, `Content-Length` 하나, `Connection: close` |
| P10 | 1xx, `Transfer-Encoding`, 값이 다른·음수·빈 `Content-Length`, `HTTP/2`, 두·네 자리 상태, 600, 쓰레기, 잘못된 이름, 빈 줄 없음 | 모두 -1 |
| P11 | 백엔드 2대에 GET 3번, 이진 POST 1번 | b1, b2, b1, b2 순서. 백엔드가 받은 요청은 `HTTP/1.0`, `Connection: close`, 원래 `Host` 없음. POST 본문 5바이트(NUL, CR 포함) 그대로 |
| P12 | 길이 없는 100,000바이트 응답 | 본문 100,000바이트 그대로, 반환 0 |
| P13 | 닫힌 포트 | 150ms 안에 `502 Bad gateway` |
| P14 | 응답 없이 끊김, 깨진 상태 줄, `Transfer-Encoding: chunked` 응답 | 모두 502 |
| P15 | 500ms 뒤에 응답하는 백엔드 | 450ms 안에 `504 Gateway timeout` |
| P16 | `Content-Length: 2`를 보내고 1초 동안 연결을 잡는 백엔드 | 150ms 안에 200 완료 |
| P17 | `Content-Length: 10` 헤더 뒤 3바이트만 보내고 끊는 백엔드 | 클라이언트는 헤더와 `abc`만 받음(502를 덧붙이지 않음), 반환 -1 |

연결 단계의 `poll()` 시간 초과(패킷을 버리는 백엔드)는 루프백에서 재현할 수 없어 단위 테스트에서 뺐습니다. 받기만 하고 응답하지 않는 백엔드의 504는 P15와 아래 F05가 확인합니다.

`test_http_response`의 R09는 `502`/`504` 상태 메시지를, `test_static_file`의 S18은 `static_has_bad_segments()`를 확인합니다.

## 기능 테스트

같은 컨테이너에서 백엔드 `httpd` 두 대와 앞단 프록시를 띄웁니다.

```sh
for n in 1 2; do mkdir -p /tmp/be$n/api; echo "be$n" > /tmp/be$n/api/who.txt; done
mkdir -p /tmp/front && echo front > /tmp/front/index.html
./build/httpd 8001 /tmp/be1 & ./build/httpd 8002 /tmp/be2 &
./build/httpd 8000 /tmp/front --route /api=127.0.0.1:8001,127.0.0.1:8002 --route /down=127.0.0.1:8009 &
```

| ID | 절차 | 기대 결과 |
| --- | --- | --- |
| F01 | `curl -s http://127.0.0.1:8000/api/who.txt` 4번 | `be1`, `be2`, `be1`, `be2` |
| F02 | `curl -s http://127.0.0.1:8000/` | `front`. 라우트가 없으면 기존 정적 서빙 |
| F03 | `curl -s -w '%{http_code}' http://127.0.0.1:8000/down` | 곧바로 `502`, 서버 `stderr`에 `phase=connect ... Connection refused` |
| F04 | `curl --path-as-is http://127.0.0.1:8000/api/../x`, `curl -X PUT .../api/who.txt`, `.../api/nope` | 403, 501, 백엔드의 404 그대로 |
| F05 | 연결만 받고 응답하지 않는 리스너(포트 8009)로 `--route /mute=127.0.0.1:8009`를 걸고 `curl .../mute` | 약 5초 뒤 `504`, `stderr`에 `phase=read_head` |
| F06 | `./build/httpd 1 /tmp --route nope`, `--bogus x`, 인자 1개 | 원인 출력 후 종료 코드 1 |
| F07 | `/api/who.txt`에 `curl` 50개 동시 | `be1` 25개, `be2` 25개 |
| F08 | `wrk -t2 -c20 -d2s http://127.0.0.1:8000/api/who.txt` | `Socket errors`, `Non-2xx` 없음. 끝날 때 `wrk`가 끊은 연결의 `phase=stream ... Broken pipe` 기록은 정상 |
| F09 | 프록시에 `kill -TERM` | 종료 코드 0 |
| F10 | `-fsanitize=address,undefined`로 빌드해 `ctest` | 6개 통과, sanitizer 보고 없음 |

F10: 커널의 `vm.mmap_rnd_bits`가 큰 Docker Desktop에서는 ASan 바이너리가 시작하자마자 `AddressSanitizer:DEADLYSIGNAL`을 반복하며 멈춥니다. 이 프로젝트 코드와 무관하며 기존 `test_cgi`도 같습니다. `docker run --security-opt seccomp=unconfined ...` 안에서 `setarch "$(uname -m)" -R ctest`로 ASLR을 끄고 실행합니다.
