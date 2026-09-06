CC = gcc
EVAL_CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror
EVAL_COMMON = tools/eval_common.c tools/eval_common.h
PREFIX ?= /usr/local
ASFLAGS = -g -Wa,--fatal-warnings
NATIVE = $(wildcard native/*.s native/*.inc native/*.asm)
.DELETE_ON_ERROR:

.PHONY: all test smoke eval stdlib test-stdlib install clean
all: build/lm0 stdlib

build/lm0: Makefile $(NATIVE) native/defaults.conf build/stdlib/catalog.inc
	mkdir -p build
	$(CC) $(ASFLAGS) -no-pie -Wl,-z,noexecstack native/lm0.s -o $@

build/library-gen: tools/library_gen.c $(EVAL_COMMON) native/hash.s
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) tools/library_gen.c tools/eval_common.c native/hash.s -ljson-c -o $@

build/stdlib/catalog.inc build/stdlib/std.h build/stdlib/catalog.id &: stdlib/catalog.json build/library-gen
	mkdir -p build/stdlib
	./build/library-gen stdlib/catalog.json build/stdlib

STDLIB_SOURCES = $(wildcard stdlib/*.lm0)
STDLIB_OBJECTS = $(patsubst stdlib/%.lm0,build/stdlib/%.o,$(STDLIB_SOURCES))
stdlib: build/stdlib/verified

build/stdlib/%.o: stdlib/%.lm0 build/lm0
	./build/lm0 build $< --kind object -o $@

build/stdlib/platform.o: stdlib/platform.c build/stdlib/std.h
	$(CC) $(EVAL_CFLAGS) -fPIC -Ibuild/stdlib -c $< -o $@

build/stdlib/liblm0std.a: $(STDLIB_OBJECTS) build/stdlib/platform.o build/stdlib/catalog.id
	rm -f $@.tmp
	ar rcs $@.tmp $(STDLIB_OBJECTS) build/stdlib/platform.o
	mv $@.tmp $@

build/library-check: tools/library_check.c $(EVAL_COMMON)
	$(CC) $(EVAL_CFLAGS) tools/library_check.c tools/eval_common.c -ljson-c -o $@

build/library-measure: tools/library_measure.c $(EVAL_COMMON) native/hash.s
	$(CC) $(EVAL_CFLAGS) tools/library_measure.c tools/eval_common.c native/hash.s -ljson-c -o $@

build/stdlib/verified: build/library-check build/stdlib/liblm0std.a build/lm0 stdlib/catalog.json
	./build/library-check stdlib/catalog.json ./build/lm0 build/stdlib/liblm0std.a $(STDLIB_SOURCES)
	touch $@

build/stdlib-test: tests/stdlib_test.c build/stdlib/liblm0std.a build/stdlib/std.h
	$(CC) $(EVAL_CFLAGS) -Ibuild/stdlib $< build/stdlib/liblm0std.a -lm -Wl,--wrap=calloc,--wrap=realloc,--wrap=free -o $@

build/library-tools-test: tests/library_tools.c $(EVAL_COMMON) native/hash.s
	$(CC) $(EVAL_CFLAGS) -Itools tests/library_tools.c tools/eval_common.c native/hash.s -ljson-c -o $@

test-stdlib: stdlib build/stdlib-test build/library-tools-test build/library-measure
	./build/stdlib-test
	./build/library-tools-test

smoke: build/lm0
	sh tests/native-smoke.sh ./build/lm0

test: smoke test-stdlib
	python3 -m unittest discover -s tests -v

eval: build/lm0 build/lm0-eval build/lm0-eval-driver

build/lm0-eval: tools/eval.c tools/eval_oracles.c $(EVAL_COMMON) native/hash.s
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) tools/eval.c tools/eval_oracles.c tools/eval_common.c native/hash.s -ljson-c -o $@

build/lm0-eval-driver: tools/eval_driver.c $(EVAL_COMMON)
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) tools/eval_driver.c tools/eval_common.c -ljson-c -ldl -o $@

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/lm0 $(DESTDIR)$(PREFIX)/bin/lm0
	install -d $(DESTDIR)$(PREFIX)/lib/lm0
	install -m 644 build/stdlib/liblm0std.a build/stdlib/catalog.id build/stdlib/std.h $(DESTDIR)$(PREFIX)/lib/lm0/

clean:
	rm -f build/lm0 build/library-gen build/library-check build/library-measure build/library-tools-test build/stdlib-test
	rm -f build/stdlib/*.o build/stdlib/*.lmi build/stdlib/catalog.inc build/stdlib/catalog.id build/stdlib/std.h build/stdlib/reference.md build/stdlib/liblm0std.a build/stdlib/liblm0std.a.tmp build/stdlib/verified
