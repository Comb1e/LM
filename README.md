# LM0

LM0 is an experimental, low-level programming language for LLM generation and
repair. Programs use typed virtual registers, basic blocks, explicit memory
operations, native pointers, and manual allocation. Its compiler is written in
GNU x86-64 assembly, emits assembly directly, and uses GCC/binutils only to
assemble and link executables, C-linkable objects, or shared libraries.

The design targets reliable generation by existing LLMs. Its advantage over C
is a hypothesis, not an established benchmark result. Explicit dependencies,
a small instruction vocabulary, and machine-readable errors are the main
design choices. Version 2 reduces repeated types and constant temporaries and
adds compact repair context. Token efficiency is measured with a named tokenizer;
source character counts alone do not establish an LLM advantage.

## Build and Run

The compiler requires x86-64 Linux, glibc, GNU make, GCC, binutils, and the `json-c`
development package for the C catalogue generator. Python is
not used to build or run the compiler and is not part of LM0 compilation.

```sh
make
./build/lm0 check examples/add.lm0
./build/lm0 run examples/add.lm0
./build/lm0 run examples/v2/count.lm0
./build/lm0 build examples/strings.lm0 -o build/strings
./build/strings
```

The addition example returns `42` as its process exit code. `lm0 run` emits
JSON containing that exit code, captured output, and execution status. The
strings executable prints `Hello from LM0` and returns its byte length, `14`.

Install the native command under `PREFIX/bin` (default `/usr/local/bin`):

```sh
make
sudo make install
lm0 run examples/linked_list.lm0
```

Version 0.4 supports `-O0`. Higher optimization levels and `--sanitize` are
rejected with `E_UNSUPPORTED`; native optimization and sanitizer instrumentation
remain future backend work.

## Language

```text
module example version 2

fn @add(%a:i32, %b:i32) -> i32 {
^entry:
    %sum = add %a, %b
    return %sum
}

fn @main() -> i32 {
^entry:
    %result = call @add(20, 22)
    return %result
}
```

- Registers have one definition within each block; other blocks can reuse names.
- Result types can be inferred locally; operands are registers or typed/contextual
  literals. Function and block interfaces remain explicit. There are no implicit casts.
- Block parameters carry values between blocks, including loop iterations.
- Integers have explicit width and signedness. Arithmetic wraps; invalid integer
  division and shifts trap. Conversions are explicit.
- Memory is mutable. Stack, heap, arrays, struct layout, pointer arithmetic,
  load/store, and byte copying are directly exposed.
- Imported/exported C functions use scalar and pointer signatures. There is no
  garbage collector, interpreter, implicit allocation, or hidden bounds checking.

Read the [language specification](docs/spec.md), [grammar](docs/grammar.ebnf),
[instruction table](docs/instructions.md), and [LLM instruction sheet](docs/llm.md).

## Standard Library

Ten bundled modules provide bytes/buffers, UTF-8 text, i64 vectors, byte-key maps,
JSON, checked arithmetic/libm, deterministic random numbers, file I/O, time, and
shared allocation/status conventions. Algorithms are written in LM0 v2; C handles
allocation and platform services. Add `use std_MODULE` to get exact signatures and
automatic linking, then retrieve only the contracts needed for the current task:

```sh
build/lm0 library list
build/lm0 library describe std_vec std_vec_push std_vec_get
build/lm0 run examples/stdlib/word_count.lm0
build/lm0 run examples/stdlib/json_transform.lm0
build/lm0 run examples/stdlib/statistics.lm0
make test-stdlib
```

The examples use local fixtures; the transformer writes `build/stdlib-transformed.json`.
See the [library guide](docs/libraries.md), [examples](examples/stdlib/README.md),
and [measurements and application report](docs/experience/libraries.md).

## Play Snake

Snake is a complete application with its game rules and state machine written
in LM0. Its existing demonstration host is still Python-based and is outside the
native compiler migration in version 0.2:

```sh
python3 -m examples.snake.server
```

Open <http://127.0.0.1:4173>. Use `--port 4175` if that port is occupied. No npm
install or external service is needed to play. It includes classic and wrap
modes, adjustable pace, keyboard/swipe/touch input, sound, and local score history.
See [the game guide](examples/snake/README.md) for controls, configuration,
architecture, and tests. The project led to [two LM0 improvements](docs/experience/snake.md):
shared-library builds and the overlap-safe `move` instruction.

## Reporting Problems

LLMs and human users can report language, compiler, tooling, and documentation
problems through the [issue-reporting workflow](docs/reporting.md). It provides
GitHub submission commands, a report template, and an offline fallback.

## Tools

```sh
lm0 emit-asm examples/array_sum.lm0 --entry -o build/array_sum.s
lm0 describe add offset cast
lm0 migrate examples/array_sum.lm0 -o build/array_sum.v2.lm0
lm0 inspect build/array_sum.v2.lm0 --function sum --block step --view compact
lm0 replace program.lm0 --function sum --block step --replacement step.txt --expect-revision HASH -o repaired.lm0
lm0 build examples/ffi.lm0 --kind object -o build/ffi.o
lm0 build examples/ffi.lm0 --kind shared -o build/ffi.so
gcc examples/ffi_driver.c build/ffi.o -o build/ffi-demo
./build/ffi-demo
```

`replace` requires a syntactically parseable original, accepts exactly one named
function or block, and checks the whole resulting module before writing. It can
repair a module that fails semantic checking. Syntax errors can be repaired by
editing source or supplying a complete corrected module.

Use the inspection's `revision` for HASH. A mismatch returns `E_STALE` without
writing. Compact inspection supplies selected source, dependency signatures,
incoming edges, and validation status; it works on parseable invalid modules.
Read `validation` and `unresolved` before relying on the context. The default
full view includes instruction descriptions and data contents. Version 1 remains
accepted; migration preserves names and validates output before writing.

`check`, `build`, `run`, `inspect`, and `replace` produce JSON on stdout. Compiler
and tool failures exit with status `2`; `run` exits with `3` on a trap, signal,
timeout, or output limit. A normally completed program's return
code is data in the `exit_code` field, so returning `42` does not fail the CLI.
Argument errors also use structured JSON and status `2`.

Compiler defaults and limits are embedded from `native/defaults.conf`. Override
selected values with `lm0 --config settings.toml ...`. Build and run outputs are
bounded; `run --timeout SECONDS` overrides the execution deadline. `--link FILE`
and `--library NAME` add native link inputs. Only `-O0` is currently accepted.

## Offline V2 Evaluation

The new evaluator is C, with `json-c` as its only additional library. The
compiler continues to require only libc and the native toolchain.

```sh
make eval
build/lm0-eval export build/evaluation
build/lm0-eval report build/evaluation -o build/evaluation-report.json
```

Exports contain paired v1/v2 examples, 20 generation tasks, 10 repair tasks,
language guidance, context, and replacement texts. Supply saved attempts at
export with `--attempts attempts.json`; `report --execute` compiles them with
the native compiler and checks return values and array mutations in a C driver.
`report --counts counts.json` imports actual tokenizer measurements bound to
artifact SHA-256 hashes. Missing token counts remain unknown. See the
[protocol and formats](docs/evaluation.md).
The [initial measurements](docs/experience/v2.md) show 18.3% fewer source bytes
across eight examples; real token savings and model accuracy remain unmeasured.

## Legacy Evaluation

```sh
python3 -m pip install -e . --no-build-isolation
lm0-bench export build/benchmark
lm0-bench grade responses.jsonl -o build/report.json
lm0-bench grade responses.jsonl --execute --sanitize -o build/report.json
```

The harness contains 20 generation tasks, 10 repair exercises, separate evaluation
cases, paired LM0/C interfaces, and independent result oracles. It consumes saved
model responses and optionally saved execution results. No API keys, network
requests, model training, or invented token measurements are involved. The
optional Python harness invokes the native executable for all LM0 checking and
compilation. See
[the benchmark protocol](docs/benchmark.md) for formats and comparison limits.

## Tests and Limits

```sh
make smoke
make eval
make test
```

`make smoke` builds and exercises the compiler without Python. `make test` also
runs the Python reference and benchmark test orchestration. The tests exercise
numeric boundaries, all integer widths, control flow,
recursion, layout, memory, C interoperability, atomic repair, execution limits,
and benchmarking. Legacy reference tests compare `-O0` and `-O2` and use
AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer needs an
environment without ptrace; run the suite outside a traced sandbox if needed.

Invalid memory access, dangling pointers, and incorrect C ABI declarations can
crash or corrupt the process. Subprocess timeouts are not a security sandbox.
Only execute trusted native code, or supply an external isolation environment
for untrusted model submissions. The reference sanitizer tests diagnose many
errors but do not make native pointers safe.

Version 1 targets x86-64 Linux, glibc, and the GNU assembler/linker. Cross-target
support, native optimization, sanitizer instrumentation, modules/link-time
language checking, variadic FFI, callbacks, atomics, threads, exceptions, and
inline assembly are deferred. See [the native compiler guide](docs/native.md).
