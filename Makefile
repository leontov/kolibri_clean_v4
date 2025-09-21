CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L
INCLUDES = -Ibackend/include

OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)

ifeq ($(strip $(OPENSSL_LIBS)),)
OPENSSL_CFLAGS += -I/opt/homebrew/opt/openssl@3/include
OPENSSL_LIBS = -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
endif

LIB_SRCS = \
backend/src/dsl.c \
backend/src/fractal.c \
backend/src/fmt_v5.c \
backend/src/core_crypto.c \
backend/src/core_run.c \
backend/src/core_verify.c \
backend/src/core_replay.c

LIB_OBJS = $(LIB_SRCS:.c=.o)
CLI_OBJ = backend/src/main_cli.o

TEST_BINS = tests/test_payload tests/test_fa tests/test_verify_break

.PHONY: all clean test

all: kolibri

kolibri: libkolibri.a $(CLI_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -o $@ $(CLI_OBJ) libkolibri.a $(OPENSSL_LIBS) -lm

libkolibri.a: $(LIB_OBJS)
	@rm -f $@
	ar rcs $@ $(LIB_OBJS)

backend/src/%.o: backend/src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -c $< -o $@

$(TEST_BINS): libkolibri.a

tests/test_payload: tests/test_payload.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -o $@ $< libkolibri.a $(OPENSSL_LIBS) -lm

tests/test_fa: tests/test_fa.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -o $@ $< libkolibri.a $(OPENSSL_LIBS) -lm

tests/test_verify_break: tests/test_verify_break.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPENSSL_CFLAGS) -o $@ $< libkolibri.a $(OPENSSL_LIBS) -lm

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	echo "Running $$t"; \
	./$$t || exit 1; \
	done

clean:
	rm -f kolibri libkolibri.a $(LIB_OBJS) $(CLI_OBJ) $(TEST_BINS)

