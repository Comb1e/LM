# Native LM0 Compiler

LM0 0.2's supported compiler is an x86-64 System V executable written in GNU
assembly. It parses, verifies, inspects, repairs, and lowers LM0 without Python or
generated C. Its output is GNU assembly; GCC is invoked as the assembler/linker
driver and verifies the configured `x86_64-linux-gnu` target first.

## Bootstrap

```sh
make
make smoke
```

The build consumes only `native/*.s`, `native/*.inc`, `native/*.asm`, and the
embedded defaults file. `make smoke` uses the shell and native toolchain only;
it neither discovers nor invokes a Python executable.

`make install PREFIX=/desired/prefix` installs `PREFIX/bin/lm0`. The optional
Python package contains the historical reference implementation, test helpers,
and offline benchmark orchestrator. It deliberately does not install an `lm0`
command; `lm0-bench` invokes the native executable for LM0 candidates.

## Architecture

- `frontend.s` tokenizes UTF-8 source and builds arena-owned module records.
- `verify.s` resolves symbols/registers and computes System V storage layouts.
- `backend.s` assigns virtual registers to frame slots and emits position-independent
  assembly, direct control flow, ABI calls, and runtime trap thunks.
- `tooling.s`, `process.s`, and `inspect.s` implement configuration, atomic output,
  bounded subprocesses, JSON command results, inspection, and validated repair.
- `ops.inc` is the shared opcode metadata used by parsing, verification, emission,
  and inspection. `runtime.asm` is embedded verbatim into emitted modules.

Compiler allocations use a process-lifetime arena. Generated LM0 programs keep
their explicit `alloc`/`free` model. Values use eight-byte frame slots; narrow
integers are normalized after definitions, and block arguments are staged in a
temporary frame area to preserve simultaneous assignment.

## Current Limits

Only `-O0` is accepted. `-O1`, `-O2`, `-O3`, `-Os`, and `--sanitize` return a
structured `E_UNSUPPORTED` diagnostic. Version 0.2 targets x86-64 Linux with
glibc and GNU binutils. The compiler reports the first source diagnostic and
keeps the existing configuration field for diagnostic-count compatibility.

The native process runner bounds combined stdout/stderr, enforces deadlines,
and kills the launched process group. This is operational containment, not a
security sandbox; compiled programs still have the invoking user's privileges.
