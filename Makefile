CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -pedantic -Ibackend/include -D_DEFAULT_SOURCE
LDFLAGS := -lpthread -lm

OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)

ifeq ($(strip $(OPENSSL_CFLAGS)),)
OPENSSL_CFLAGS := -I/opt/homebrew/opt/openssl@3/include -I/usr/local/opt/openssl@3/include
endif

ifeq ($(strip $(OPENSSL_LIBS)),)
OPENSSL_LIBS := -L/opt/homebrew/opt/openssl@3/lib -L/usr/local/opt/openssl@3/lib -lssl -lcrypto
else
OPENSSL_LIBS += -lssl -lcrypto
endif

SRC := \
	backend/src/dsl.c \
	backend/src/fractal.c \
	backend/src/fmt_v5.c \
	backend/src/core_common.c \
	backend/src/core_run.c \
	backend/src/core_verify.c \
	backend/src/core_replay.c \
	backend/src/http.c \
	backend/src/main_cli.c

OBJ := $(SRC:.c=.o)
TARGET := kolibri

TEST_SRC := \
	tests/test_payload.c \
	tests/test_fa.c \
	tests/test_verify_break.c

TEST_BIN := $(TEST_SRC:.c=)

.PHONY: all build clean distclean test web-dev web-build web-clean serve

all: build

build: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(OPENSSL_LIBS)

backend/src/%.o: backend/src/%.c backend/include/%.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/main_cli.o: backend/src/main_cli.c
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/http.o: backend/src/http.c backend/include/http.h backend/include/core.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/core_run.o: backend/src/core_run.c backend/include/core.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/core_verify.o: backend/src/core_verify.c backend/include/core.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/core_replay.o: backend/src/core_replay.c backend/include/core.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/core_common.o: backend/src/core_common.c backend/include/core.h backend/include/dsl.h backend/include/fractal.h backend/include/fmt_v5.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/dsl.o: backend/src/dsl.c backend/include/dsl.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/fractal.o: backend/src/fractal.c backend/include/fractal.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

backend/src/fmt_v5.o: backend/src/fmt_v5.c backend/include/fmt_v5.h
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) -c $< -o $@

$(TEST_BIN): %: %.c $(TARGET)
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) $< \
		backend/src/dsl.o backend/src/fractal.o backend/src/fmt_v5.o \
		backend/src/core_common.o backend/src/core_verify.o backend/src/core_run.o \
		-o $@ $(OPENSSL_LIBS) -lm

test: build $(TEST_BIN)
	./tests/test_payload
	./tests/test_fa
	./tests/test_verify_break

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN)

web-dev:
	cd web && npm run dev

web-build:
	cd web && npm install && npm run build

web-clean:
	rm -rf web/node_modules web/dist

distclean: clean web-clean

serve: build
	./$(TARGET) serve --port 8080 --static web/dist
