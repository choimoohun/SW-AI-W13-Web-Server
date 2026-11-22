# C-04: make / make test / make asan / make clean 지원. (+ make integration)
# 오늘 범위: 정적 파일 서버. 소스 src/, 헤더 include/.
CC      ?= cc
CSTD    ?= -std=c11
CFLAGS  ?= $(CSTD) -Wall -Wextra -Werror -D_XOPEN_SOURCE=700 -Iinclude -MMD -MP
LDFLAGS ?=
LDLIBS  ?= -pthread   # 데모 스레드/저장소 락 링크용

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)
BIN  := webserver
TESTBIN := tests/bin

.PHONY: all test integration test-flight integration-flight integration-persist test-tracking integration-tracking integration-req integration-backends load-test leak asan tsan clean

all: $(BIN)

-include $(DEPS)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 단위 테스트 (C-04, §7): 파서 + 경로보안/MIME.
test:
	@mkdir -p $(TESTBIN)
	$(CC) $(CFLAGS) -o $(TESTBIN)/test_parser tests/unit/test_parser.c src/http_parser.c
	@$(TESTBIN)/test_parser
	$(CC) $(CFLAGS) -o $(TESTBIN)/test_static tests/unit/test_static.c src/static_handler.c
	@$(TESTBIN)/test_static

# 기능(통합) 테스트: 서버를 띄우고 curl 로 정적 서빙 검증.
integration: $(BIN)
	@sh tests/integration/run.sh

# 항공편 저장소 경쟁 테스트: before=초과예약 / after=1건만 성공.
test-flight:
	@mkdir -p $(TESTBIN)
	$(CC) $(CFLAGS) -pthread -o $(TESTBIN)/test_flight_race tests/unit/test_flight_race.c src/flight_store.c
	FLIGHT_DATA_FILE=/dev/null $(TESTBIN)/test_flight_race

# 항공편 API 통합: 100동시 예약 + 출발/도착 추적.
integration-flight: $(BIN)
	@sh tests/integration/flight_run.sh

# 출발/도착 시간 추적: 가상 시계 배속 전이 검증.
test-tracking:
	@mkdir -p $(TESTBIN)
	$(CC) $(CFLAGS) -pthread -o $(TESTBIN)/test_flight_tracking tests/unit/test_flight_tracking.c src/flight_store.c src/flight_clock.c
	FLIGHT_DATA_FILE=/dev/null FLIGHT_TIME_MODE=sim FLIGHT_SIM_SPEED=1000 $(TESTBIN)/test_flight_tracking

integration-tracking: $(BIN)
	@sh tests/integration/flight_tracking.sh

# 요구사항 통합: CLI/헤더/health/echo/프록시/로그/CDN.
integration-req: $(BIN)
	@sh tests/integration/requirements.sh

# 별도 백엔드 서비스(미니 레디스/미니 DBMS) 연결 + 리소스 제한 + 독립 재시작 시연.
integration-backends: $(BIN)
	@sh tests/integration/backend_services.sh

# 부하 테스트: 200요청 동시 20, RPS/지연/에러 수 기록.
load-test: $(BIN)
	@sh tests/integration/load_test.sh

# 영속 저장 검증: 재시작 후에도 예약/좌석/이력 유지.
integration-persist: $(BIN)
	@sh tests/integration/flight_persist.sh

# ThreadSanitizer 빌드 (before 모드의 데이터 레이스 확인용).
tsan:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CSTD) -Wall -Wextra -Werror -D_XOPEN_SOURCE=700 -Iinclude -fsanitize=thread -g" all
	@echo "[tsan] ./webserver 실행 후 before/after 시나리오를 돌려 리포트 확인."

# 누수 검사 (Linux valgrind, Docker). Docker Desktop 실행 필요.
leak:
	@docker info >/dev/null 2>&1 || { echo "Docker 데몬이 꺼져 있습니다. 'open -a Docker'로 실행하세요."; exit 1; }
	docker compose run --rm dev sh /workspace/tests/leak_check.sh

# AddressSanitizer + UBSan 빌드(NFR-01).
# macOS 는 메모리오류/UBSan 검사만 지원(누수검사 Linux 전용) → 누수 0 확정은 Docker valgrind.
asan:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CSTD) -Wall -Wextra -Werror -D_XOPEN_SOURCE=700 -Iinclude -fsanitize=address,undefined -g" all
	@echo "[asan] ./$(BIN) 실행 후 시나리오 실행. macOS=메모리오류 검사, 누수 0 확정=Linux valgrind."

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)
	rm -rf $(TESTBIN)
