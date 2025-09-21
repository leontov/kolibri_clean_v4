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
TEST_BIN := $(BIN_DIR)/reason_payload_test

all: $(BIN_DIR) $(BINS)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BIN_DIR)/kolibri_run: $(SRC)/main_run.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/kolibri_verify: $(SRC)/main_verify.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/kolibri_replay: $(SRC)/main_replay.c $(SRC)/core.c $(SRC)/dsl.c $(SRC)/chainio.c $(SRC)/reason.c $(SRC)/config.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): backend/tests/reason_payload_test.c $(SRC)/reason.c
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@ $(LDFLAGS)

.PHONY: tests
tests: $(BIN_DIR) $(TEST_BIN)
	python3 tests/test_reason_payload.py

clean:
	rm -rf $(BIN_DIR) logs/*.jsonl logs/*.json
