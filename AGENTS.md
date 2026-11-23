# AGENTS.md

C17/POSIX 웹 서버 과제입니다. 요구사항은 `docs/웹 서버 만들기.md`, 단계 순서와 진행 상태는 `docs/README.md`, 단계별 설계는 `docs/<단계>/`에 있습니다. 구현은 해당 설계 문서를 따릅니다.

## 빌드·테스트

Windows 네이티브 빌드는 안 됩니다. Docker 또는 WSL2에서 실행합니다.

```sh
docker compose run --build --rm dev        # Docker로 구성·빌드
docker compose run --rm dev sh             # Docker 셸

cmake -S . -B build && cmake --build build # 네이티브
(cd build && ctest --output-on-failure)
(cd build && ctest -R <이름> --output-on-failure)  # 단일 테스트
```

서버와 클라이언트를 따로 띄울 때는 같은 컨테이너에 붙습니다: `docker compose run --rm --name web-dev dev sh` 후 `docker exec -it web-dev sh`.

## 지켜야 할 것

- 표준 C와 POSIX API만 사용합니다. HTTP·이벤트 루프 라이브러리는 금지입니다.
- C17, `CMAKE_C_EXTENSIONS OFF`, `_POSIX_C_SOURCE=200809L`, CMake 3.16 호환을 유지합니다.
- 테스트는 CTest에 등록합니다.
- 한 번에 한 단계만 구현합니다. 설계 문서 범위 표의 '제외' 항목과 `docs/README.md`에서 미설계인 단계는 구현하지 않습니다.
- 설계와 다르게 구현해야 하면 코드보다 설계 문서를 먼저 고칩니다. 단계가 끝나면 `docs/README.md`의 상태 칸을 고칩니다.
- `.h`에 선언하는 함수에는 선언 바로 위에 주석을 답니다. 무엇을 하는지, 반환값과 실패 시 동작(`errno`, `-1` 등), 호출자가 해제해야 하는 자원을 적습니다. `static` 함수와 교재에서 가져온 `csapp.*`에는 달지 않습니다.
- 문서와 PR은 한국어로 쓰고, PR은 `.github/pull_request_template.md`를 따릅니다.
