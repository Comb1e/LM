CC = gcc
PREFIX ?= /usr/local
ASFLAGS = -g -Wa,--fatal-warnings
NATIVE = $(wildcard native/*.s native/*.inc native/*.asm)

.PHONY: all test smoke eval install clean
all: build/lm0

build/lm0: Makefile $(NATIVE) native/defaults.conf
	mkdir -p build
	$(CC) $(ASFLAGS) -no-pie -Wl,-z,noexecstack native/lm0.s -o $@

smoke: build/lm0
	sh tests/native-smoke.sh ./build/lm0

test: smoke
	python3 -m unittest discover -s tests -v

EVAL_CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror
EVAL_COMMON = tools/eval_common.c tools/eval_common.h
eval: build/lm0 build/lm0-eval build/lm0-eval-driver

build/lm0-eval: tools/eval.c tools/eval_oracles.c $(EVAL_COMMON) native/hash.s
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) tools/eval.c tools/eval_oracles.c tools/eval_common.c native/hash.s -ljson-c -o $@

build/lm0-eval-driver: tools/eval_driver.c $(EVAL_COMMON)
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) tools/eval_driver.c tools/eval_common.c -ljson-c -ldl -o $@

install: build/lm0
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/lm0 $(DESTDIR)$(PREFIX)/bin/lm0

clean:
	rm -f build/lm0
