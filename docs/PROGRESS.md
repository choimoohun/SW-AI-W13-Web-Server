# 프로젝트 진행 상태

- **현재 단계** … 1.4 정적 파일 서빙. [단계별 완료 기준](SPEC.md#작업-단위와-완료-기준).
- **마지막으로 검증한 내용** … Docker에서 `cmake --build /tmp/week13-static` 및 `ctest --test-dir /tmp/week13-static --output-on-failure`를 실행했다. 분할 POST와 정적 파일 테스트 2개가 통과했다. HTML·PNG의 MIME과 길이·바이트 일치, 4 KiB보다 큰 파일, `403`·`404`·`500` 응답을 확인했다.
- **현재 작업** … [HTTP 코드](../src/http.c)는 GET에 정적 파일을 보내고 POST에는 받은 본문을 돌려준다. [예매 화면](../public/index.html)의 Chrome 렌더링과 좌석 선택은 수동 검증이 남았다.
- **막힌 부분** … 현재 확인된 차단 요인은 없다.
- **다음 작업 한 가지** … [README](../README.md)의 명령으로 서버를 켠 뒤 Chrome에서 예매 화면을 열고, 마우스와 키보드로 좌석을 선택해 요약이 바뀌는지 확인한다.
