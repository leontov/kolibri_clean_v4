SHELL := /bin/sh
CC := cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
INCLUDE := -Ibackend/include

OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS   := $(shell pkg-config --libs openssl 2>/dev/null)
ifeq ($(OPENSSL_LIBS),)
OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(OPENSSL_PREFIX),)
OPENSSL_CFLAGS := -I$(OPENSSL_PREFIX)/include
OPENSSL_LIBS   := -L$(OPENSSL_PREFIX)/lib -lcrypto
else
OPENSSL_LIBS   := -lcrypto
endif
endif

CFLAGS  += $(OPENSSL_CFLAGS)
LDFLAGS := $(OPENSSL_LIBS) -lm

SRC := backend/src
BIN_DIR := bin
BINS := $(BIN_DIR)/kolibri_run $(BIN_DIR)/kolibri_verify $(BIN_DIR)/kolibri_replay

TEST_BINS := $(BIN_DIR)/reason_payload_test $(BIN_DIR)/bench_validation_test $(BIN_DIR)/fractal_test $(BIN_DIR)/test_verify_break

COMMON_OBJS := $(SRC)/digit_agents.c $(SRC)/vote_aggregate.c


all: $(BIN_DIR) $(BINS)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)


$(BIN_DIR)/kolibri_run: $(SRC)/main_run.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c $(SRC)/fractal.c $(COMMON_OBJS)

$(BIN_DIR)/kolibri_run: $(SRC)/main_run.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c $(SRC)/fractal.c $(SRC)/digit_agents.c $(SRC)/vote_aggregate.c

	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/kolibri_verify: $(SRC)/main_verify.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c $(SRC)/digit_agents.c $(SRC)/vote_aggregate.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)


$(BIN_DIR)/kolibri_replay: $(SRC)/main_replay.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c $(SRC)/fractal.c $(COMMON_OBJS)

$(BIN_DIR)/kolibri_replay: $(SRC)/main_replay.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c $(SRC)/fractal.c $(SRC)/digit_agents.c $(SRC)/vote_aggregate.c

	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/test_vote: backend/tests/test_vote.c $(SRC)/digit_agents.c $(SRC)/vote_aggregate.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/reason_payload_test: backend/tests/reason_payload_test.c $(SRC)/reason.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/bench_validation_test: backend/tests/bench_validation_test.c $(SRC)/reason.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/fractal_test: backend/tests/fractal_test.c $(SRC)/fractal.c $(SRC)/dsl.c $(SRC)/reason.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/test_verify_break: backend/tests/test_verify_break.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

.PHONY: tests
tests: $(BIN_DIR) $(TEST_BINS)
	python3 tests/test_reason_payload.py
	$(BIN_DIR)/bench_validation_test
	$(BIN_DIR)/fractal_test
	$(BIN_DIR)/test_verify_break

.PHONY: test
test: $(BIN_DIR)/test_vote
	$(BIN_DIR)/test_vote


clean:
	rm -rf $(BIN_DIR) logs/*.jsonl logs/*.json
