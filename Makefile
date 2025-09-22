CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic -Icore
LDFLAGS ?= -lm

CORE_SOURCES := $(filter-out core/main.c, $(wildcard core/*.c))

all: kolibri_native

kolibri_native: $(CORE_SOURCES) core/main.c
	$(CC) $(CFLAGS) core/main.c $(CORE_SOURCES) -o $@ $(LDFLAGS)

.PHONY: test_core

kolibri_native_demo: kolibri_native
	./kolibri_native --ticks 3

test_core: tests/test_core
	./tests/test_core

tests/test_core: $(CORE_SOURCES) tests/test_core.c
	$(CC) $(CFLAGS) tests/test_core.c $(CORE_SOURCES) -o $@ $(LDFLAGS)

clean:
	rm -f kolibri_native tests/test_core ui/kolibri.wasm kolibri_chain.jsonl

.PHONY: clean kolibri_native_demo
