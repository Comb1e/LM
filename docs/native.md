# Native LM0 Compiler

LM0 0.6's compiler is an x86-64 System V executable with a C V3 frontend and a GNU
assembly core. It parses, verifies, inspects, repairs, and lowers LM0 without Python
or generated C. Its output is GNU assembly; GCC is invoked as the assembler/linker
driver and verifies the configured `x86_64-linux-gnu` target first.

## Bootstrap

```sh
make
make smoke
```

The bootstrap first builds a C catalogue generator using `json-c`. It generates
the embedded standard-library metadata, C header and interface reference from
`stdlib/catalog.json`, then assembles the compiler. `make` next compiles the LM0
library algorithms and C platform adapters, creates a PIC static archive, and
checks all 86 exports/signatures. No Python is used in any of these steps.
The compiler executable itself depends only on libc; standard-library programs
use libc and libm. `json-c` is a build/tooling dependency, not a runtime dependency
of LM0 programs. `make smoke` uses the shell and native toolchain only.

`make install PREFIX=/desired/prefix` installs `PREFIX/bin/lm0` and the standard
archive, catalogue identity, and C header under `PREFIX/lib/lm0`. The optional
Python package contains the historical reference implementation, test helpers,
and offline benchmark orchestrator. It deliberately does not install an `lm0`
command; `lm0-bench` invokes the native executable for LM0 candidates.

## Architecture

- `v3.c` parses indentation, checks local types/assignment, and lowers expressions,
  slices and structured control flow through mapped V2 source text.
  `v3_bridge.s` shares the existing catalogue, parser and tooling interfaces.

- `frontend.s` tokenizes UTF-8 source and builds arena-owned module records.
- `verify.s` resolves symbols/registers and computes System V storage layouts.
- `normalize.s` infers v2 types and lowers literal operands into typed constants.
  Internal register records retain block ownership and original source spans.
- `migrate.s` applies token-span edits and validates canonical v2 output.
  `hash.s` implements SHA-256, shared with the optional C evaluator.
- `backend.s` assigns virtual registers to frame slots and emits position-independent
  assembly, direct control flow, ABI calls, and runtime trap thunks.
- `tooling.s`, `process.s`, and `inspect.s` implement configuration, atomic output,
  bounded subprocesses, JSON command results, inspection, and validated repair.
- `ops.inc` is the shared opcode metadata used by parsing, verification, emission,
  and inspection. `runtime.asm` is embedded verbatim into emitted modules.
- `library.s` resolves bundled imports through the ordinary parser, restores
  source/lexer state, supplies catalogue queries and contracts, and locates the
  archive relative to the running executable. Imported function records retain
  their origin separately from user source and frame information.

Compiler allocations use a process-lifetime arena. Generated LM0 programs keep
their explicit `alloc`/`free` model. Values use eight-byte frame slots; narrow
integers are normalized after definitions, and block arguments are staged in a
temporary frame area to preserve simultaneous assignment.

## Current Limits

Only `-O0` is accepted. `-O1`, `-O2`, `-O3`, `-Os`, and `--sanitize` return a
structured `E_UNSUPPORTED` diagnostic. Version 0.4 targets x86-64 Linux with
glibc and GNU binutils. The compiler reports the first source diagnostic and
keeps the existing configuration field for diagnostic-count compatibility.

The native process runner bounds combined stdout/stderr, enforces deadlines,
and kills the launched process group. This is operational containment, not a
security sandbox; compiled programs still have the invoking user's privileges.
