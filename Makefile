CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic
SOURCES := $(wildcard src/*.c)

.PHONY: all test clean sanitize
all: build/cairn
build/cairn: $(SOURCES) $(wildcard src/*.h)
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) -o $@
test: build/cairn
	python3 tools/test.py
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' test
clean:
	rm -rf build
