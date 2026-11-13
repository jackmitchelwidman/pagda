CC ?= cc
WARN = -std=gnu11 -Wall -Wextra -Wno-unused-parameter
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)
LIB = $(sort $(wildcard lib/*.py))

all: pyc

# Python-source standard library modules, embedded in the binary.
build/pylib.c: $(LIB) tools/embed.sh
	@mkdir -p build
	sh tools/embed.sh $(LIB) > $@

# The Agda definitions that checked files are translated against.
build/prelude.c: lib/PycPrelude.agda
	@mkdir -p build
	{ printf 'const char agda_prelude[] = {'; od -An -v -tu1 lib/PycPrelude.agda | tr -s ' \n' ',' | sed 's/^,//'; echo '0};'; } > $@

GEN = build/pylib.c build/prelude.c

pyc: $(SRC) $(HDR) $(GEN)
	$(CC) $(WARN) -O2 -g -o $@ $(SRC) $(GEN) -lm -lpthread

# Debug build: address and undefined-behavior checks.
pyc-debug: $(SRC) $(HDR) $(GEN)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $(SRC) $(GEN) -lm -lpthread

test: pyc
	sh tools/runtests.sh ./pyc

test-debug: pyc-debug
	PYC_GC_STRESS=13 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 sh tools/runtests.sh ./pyc-debug

clean:
	rm -rf pyc pyc-debug build

.PHONY: all test test-debug clean
