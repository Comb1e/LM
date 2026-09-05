# LM0

LM0 is an experimental, low-level programming language for LLM generation and
repair. Programs use typed virtual registers, basic blocks, explicit memory
operations, native pointers, and manual allocation. The compiler emits C11 and
uses GCC to produce native executables, C-linkable object files, or shared libraries.

The design targets reliable generation by existing LLMs. Its advantage over C
is a hypothesis, not an established benchmark result. Explicit dependencies,
a small instruction vocabulary, and machine-readable errors are the main
design choices. Source readability and minimum character count are not goals.

## Run

Requires Python 3.11+ and GCC on x86-64 Linux. The compiler has no third-party
Python runtime dependencies. From this checkout:

```sh
python3 -m lm0 check examples/add.lm0
python3 -m lm0 run examples/add.lm0
python3 -m lm0 build examples/strings.lm0 -O 2 -o build/strings
./build/strings
```

The addition example returns `42` as its process exit code. `lm0 run` emits
JSON containing that exit code, captured output, and execution status. The
strings executable prints `Hello from LM0` and returns its byte length, `14`.

To install the `lm0` command in an environment with setuptools 68 or newer:

```sh
python3 -m pip install -e . --no-build-isolation
lm0 run examples/linked_list.lm0 --sanitize
```

## Language

```text
module example version 1

fn @add(%a:i32, %b:i32) -> i32 {
^entry:
    %sum:i32 = add %a, %b
    return %sum
}

fn @main() -> i32 {
^entry:
    %a:i32 = const 20
    %b:i32 = const 22
    %result:i32 = call @add(%a, %b)
    return %result
}
```

- Registers have one definition; operands are registers, not nested expressions.
- Block parameters carry values between blocks, including loop iterations.
- Integers have explicit width and signedness. Arithmetic wraps; invalid integer
  division and shifts trap. Conversions are explicit.
- Memory is mutable. Stack, heap, arrays, struct layout, pointer arithmetic,
  load/store, and byte copying are directly exposed.
- Imported/exported C functions use scalar and pointer signatures. There is no
  garbage collector, interpreter, implicit allocation, or hidden bounds checking.

Read the [language specification](docs/spec.md), [grammar](docs/grammar.ebnf),
[instruction table](docs/instructions.md), and [LLM instruction sheet](docs/llm.md).

## Play Snake

Snake is a complete application with its game rules and state machine written
in LM0, hosted by a small Python server with a responsive browser UI:

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
python3 -m lm0 emit-c examples/array_sum.lm0 --entry -o build/array_sum.c
python3 -m lm0 inspect examples/array_sum.lm0 --function sum --block step
python3 -m lm0 replace program.lm0 --function sum --block step --replacement step.txt -o repaired.lm0
python3 -m lm0 build examples/ffi.lm0 --kind object -o build/ffi.o
python3 -m lm0 build examples/ffi.lm0 --kind shared -o build/ffi.so
gcc examples/ffi_driver.c build/ffi.o -o build/ffi-demo
./build/ffi-demo
```

`replace` requires a syntactically parseable original, accepts exactly one named
function or block, and checks the whole resulting module before writing. It can
repair a module that fails semantic checking. Syntax errors can be repaired by
editing source or supplying a complete corrected module.

`check`, `build`, `run`, `inspect`, and `replace` produce JSON on stdout. Compiler
and tool failures exit with status `2`; `run` exits with `3` on a trap, signal,
sanitizer failure, or execution limit. A normally completed program's return
code is data in the `exit_code` field, so returning `42` does not fail the CLI.
Argument-usage errors use argparse's conventional stderr output and status `2`.

Compiler defaults and limits are in `lm0/defaults.toml`. Override selected values
with `python3 -m lm0 --config settings.toml ...`. Build and run outputs are
bounded; `run --timeout SECONDS` overrides the execution deadline. `--link FILE`
and `--library NAME` add native link inputs. Use `-O 0/1/2/3/s` for optimization.

## Evaluation

```sh
python3 -m lm0 bench export build/benchmark
python3 -m lm0 bench grade responses.jsonl -o build/report.json
python3 -m lm0 bench grade responses.jsonl --execute --sanitize -o build/report.json
```

The harness contains 20 generation tasks, 10 repair exercises, separate evaluation
cases, paired LM0/C interfaces, and independent result oracles. It consumes saved
model responses and optionally saved execution results. No API keys, network
requests, model training, or invented token measurements are involved. See
[the benchmark protocol](docs/benchmark.md) for formats and comparison limits.

## Tests and Limits

```sh
python3 -m unittest discover -s tests -v
```

The tests exercise numeric boundaries, all integer widths, control flow,
recursion, layout, memory, C interoperability, atomic repair, execution limits,
and benchmarking. Native tests compare `-O0` and `-O2` and use AddressSanitizer
and UndefinedBehaviorSanitizer. LeakSanitizer needs an environment without
ptrace; run the suite outside a traced sandbox if it reports that limitation.

Invalid memory access, dangling pointers, and incorrect C ABI declarations can
crash or corrupt the process. Subprocess timeouts are not a security sandbox.
Only execute trusted native code, or supply an external isolation environment
for untrusted model submissions. Sanitizers diagnose many errors but do not
make native pointers safe.

Version 1 targets x86-64 Linux and the GCC toolchain. LLVM emission, cross-target
support, modules/link-time language checking, variadic FFI, callbacks, atomics,
threads, exceptions, and inline assembly are deferred.
