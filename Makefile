CC = gcc
EVAL_CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Werror
EVAL_COMMON = tools/eval_common.c tools/eval_common.h
PREFIX ?= /usr/local
ASFLAGS = -g -Wa,--fatal-warnings
NATIVE = $(wildcard native/*.s native/*.inc native/*.asm)
.DELETE_ON_ERROR:

.PHONY: all test smoke eval stdlib test-stdlib snake test-snake install clean
all: build/lm0 stdlib

build/v3.o: native/v3.c native/v3.h
	mkdir -p build
	$(CC) $(EVAL_CFLAGS) -Wno-misleading-indentation -c native/v3.c -o $@

build/lm0: Makefile $(NATIVE) native/defaults.conf build/stdlib/catalog.inc build/v3.o
	mkdir -p build
	$(CC) $(ASFLAGS) -no-pie -Wl,-z,noexecstack native/lm0.s build/v3.o -o $@

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

build/stdlib/network.o: stdlib/network.c build/stdlib/std.h
	$(CC) $(EVAL_CFLAGS) -fPIC -Ibuild/stdlib -c $< -o $@

build/stdlib/liblm0std.a: $(STDLIB_OBJECTS) build/stdlib/platform.o build/stdlib/network.o build/stdlib/catalog.id
	rm -f $@.tmp
	ar rcs $@.tmp $(STDLIB_OBJECTS) build/stdlib/platform.o build/stdlib/network.o
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

build/network-test: tests/network_test.c build/stdlib/liblm0std.a build/stdlib/std.h
	$(CC) $(EVAL_CFLAGS) -Ibuild/stdlib $< build/stdlib/liblm0std.a -lm -o $@

SNAKE_PARTS = engine_v2 support application routes
SNAKE_OBJECTS = $(addprefix build/snake/,$(addsuffix .o,$(SNAKE_PARTS)))
.PHONY: snake test-snake
snake: build/snake/snake build/snake/config.json build/snake/static/index.html

build/snake/%.o: examples/snake/%.lm0 build/lm0
	mkdir -p build/snake
	./build/lm0 build $< --kind object -o $@

build/snake/snake: examples/snake/server.lm0 $(SNAKE_OBJECTS) build/stdlib/liblm0std.a
	./build/lm0 build $< $(foreach obj,$(SNAKE_OBJECTS),--link $(obj)) -o $@

build/snake-test: tests/snake_native_test.c build/snake/engine_v2.o build/stdlib/liblm0std.a
	$(CC) $(EVAL_CFLAGS) $< build/snake/engine_v2.o build/stdlib/liblm0std.a -lm -o $@

test-snake: snake build/snake-test build/network-test
	./build/snake-test
	./build/network-test

build/snake/config.json: examples/snake/config.json
	mkdir -p build/snake
	cp $< $@

build/snake/static/index.html: $(wildcard examples/snake/static/*)
	mkdir -p build/snake/static
	cp examples/snake/static/* build/snake/static/

test-stdlib: stdlib build/stdlib-test build/library-tools-test build/library-measure build/network-test
	./build/stdlib-test
	./build/network-test
	./build/library-tools-test

smoke: build/lm0
	sh tests/native-smoke.sh ./build/lm0

test: smoke test-stdlib test-v3
	python3 -m unittest discover -s tests -v

.PHONY: test-v3
test-v3: build/lm0 build/v3-test
	./build/v3-test

build/v3-test: tests/v3_test.c $(EVAL_COMMON)
	$(CC) $(EVAL_CFLAGS) -Wno-misleading-indentation -Itools tests/v3_test.c tools/eval_common.c -ljson-c -o $@

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
	rm -f build/v3.o build/v3-test
	rm -f build/lm0 build/library-gen build/library-check build/library-measure build/library-tools-test build/stdlib-test
	rm -f build/network-test build/snake-test build/snake/snake build/snake/*.o build/snake/config.json
	rm -rf build/snake/static
	rm -f build/stdlib/*.o build/stdlib/*.lmi build/stdlib/catalog.inc build/stdlib/catalog.id build/stdlib/std.h build/stdlib/reference.md build/stdlib/liblm0std.a build/stdlib/liblm0std.a.tmp build/stdlib/verified
