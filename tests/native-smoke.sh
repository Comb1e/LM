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
"$compiler" run examples/v2/count.lm0
"$compiler" describe add cast
"$compiler" library describe std_text std_text_parse_i64
"$compiler" check examples/stdlib/statistics.lm0
"$compiler" migrate examples/add.lm0 -o "$temporary/add.v2.lm0"
"$compiler" inspect "$temporary/add.v2.lm0" --function main --block entry --view compact
"$compiler" emit-asm "$temporary/add.v2.lm0" --entry -o "$temporary/add.v2.s"
gcc "$temporary/add.v2.s" -o "$temporary/add.v2"
status=0
"$temporary/add.v2" || status=$?
test "$status" -eq 42
