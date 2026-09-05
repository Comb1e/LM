CC = gcc
PREFIX ?= /usr/local
ASFLAGS = -g -Wa,--fatal-warnings
NATIVE = $(wildcard native/*.s native/*.inc native/*.asm)

.PHONY: all test smoke install clean
all: build/lm0

build/lm0: Makefile $(NATIVE) native/defaults.conf
	mkdir -p build
	$(CC) $(ASFLAGS) -no-pie -Wl,-z,noexecstack native/lm0.s -o $@

smoke: build/lm0
	sh tests/native-smoke.sh ./build/lm0

test: smoke
	python3 -m unittest discover -s tests -v

install: build/lm0
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/lm0 $(DESTDIR)$(PREFIX)/bin/lm0

clean:
	rm -f build/lm0
