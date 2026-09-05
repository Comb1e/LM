#!/bin/sh
set -eu
compiler=${1:-./build/lm0}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
"$compiler" --version
"$compiler" check examples/add.lm0
"$compiler" emit-asm examples/add.lm0 --entry -o "$temporary/add.s"
gcc "$temporary/add.s" -o "$temporary/add"
status=0
"$temporary/add" || status=$?
test "$status" -eq 42
"$compiler" run examples/strings.lm0
"$compiler" build examples/ffi.lm0 --kind object -o "$temporary/ffi.o"
"$compiler" build examples/ffi.lm0 --kind shared -o "$temporary/ffi.so"
"$compiler" inspect examples/linked_list.lm0 --function main --block entry
