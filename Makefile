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

.PHONY: all test integration restart-test test-flight integration-flight integration-persist test-tracking integration-tracking integration-req integration-backends integration-airports integration-notifications integration-payments edge-cases mutation reset-flights load-test load-test-heavy leak asan tsan review-cases demo-up demo-down verify-demo pr-review clean

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
	$(CC) $(CFLAGS) -o $(TESTBIN)/test_route tests/unit/test_route.c src/config.c
	@$(TESTBIN)/test_route

# 기능(통합) 테스트: 서버를 띄우고 curl 로 정적 서빙 검증.
integration: $(BIN)
	@sh tests/integration/run.sh

# 재시작 시 즉시 재바인딩(SO_REUSEADDR) 검증.
restart-test: $(BIN)
	@sh tests/integration/restart.sh

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

# 공항 정보 서비스 2대: 라우팅·Round-Robin·장애 격리·독립 재시작 (외부 레포 불필요).
integration-airports: $(BIN)
	@sh tests/integration/airport_service.sh

# 결제 서비스(별도 프로세스·자기 DB·중복 방지 키·환불·독립 재시작) 연동 검증.
integration-payments: $(BIN)
	@sh tests/integration/payment_service.sh

# 결제 → 메시지 큐 → 알림 서비스 비동기 연동(아웃박스·lease 재시도·멱등).
integration-notifications: $(BIN)
	@sh tests/integration/notification_service.sh

# 예약 상태 파일을 지우고 16편 시드로 다시 시작.
reset-flights:
	rm -f data/flights.txt data/flights.txt.tmp

# 잘못된 요청/큰 본문/백엔드 없음/클라이언트 끊김 시나리오.
edge-cases: $(BIN)
	@sh tests/integration/edge_cases.sh

# 뮤테이션 테스트: 복사본에서 조건을 뒤집고 기존 테스트가 잡는지 확인.
mutation:
	@sh tests/mutation_check.sh

# 부하 테스트: 기본 200요청 동시 20, RPS/지연/에러 수 기록.
load-test: $(BIN)
	@sh tests/integration/load_test.sh

# 동시 100, 1000요청: FR-06 "100 동시 접속" 수용 기준 확인.
load-test-heavy: $(BIN)
	@LOAD_C=100 LOAD_N=1000 sh tests/integration/load_test.sh

# 검토 과제용 테스트 케이스: 트래픽 급증/느린 클라이언트/예약·결제 일관성/워커 격리.
review-cases: $(BIN)
	@sh tests/integration/review_cases.sh

# 데모 전체를 띄우고 끈다 (C 서버 + 결제 + 공항 A/B + MQ + 알림).
demo-up:
	@sh tests/integration/demo_up.sh
demo-down:
	@sh tests/integration/demo_down.sh

# 데모 구성에서 돌려야 하는 회귀 순서.
verify-demo: $(BIN)
	@$(MAKE) --no-print-directory test integration-flight integration-airports integration-notifications edge-cases

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

# PR 리뷰 코멘트를 검토 체크리스트로 모은다: make pr-review PR=12
# 결과: docs/records/pr_reviews/PR-12.md (gh auth login 필요)
pr-review:
	@test -n "$(PR)" || { echo "사용법: make pr-review PR=<번호>"; exit 1; }
	@python3 scripts/pr_review_fetch.py $(PR) $(if $(REPO),--repo $(REPO))

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)
	rm -rf $(TESTBIN)
