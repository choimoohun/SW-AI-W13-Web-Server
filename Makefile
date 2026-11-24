# C-04: make / make test / make asan / make clean 지원. (+ make integration)
# 오늘 범위: 정적 파일 서버. 소스 src/, 헤더 include/.
CC      ?= cc
CSTD    ?= -std=c11
CFLAGS  ?= $(CSTD) -Wall -Wextra -Werror -D_XOPEN_SOURCE=700 -Iinclude
LDFLAGS ?=
LDLIBS  ?=

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
BIN  := webserver
TESTBIN := tests/bin

.PHONY: all test integration leak asan clean

all: $(BIN)

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
	rm -f $(OBJS) $(BIN)
	rm -rf $(TESTBIN)
