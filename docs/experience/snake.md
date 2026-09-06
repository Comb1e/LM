# LM0 Application Report: Snake

Building [Snake](../../examples/snake/README.md) exposed several concrete
missing capabilities. They are now implemented, documented, and covered by
native regression tests. The server, session manager, HTTP parser, and game
decisions run in LM0; C is limited to operating-system adapters and JavaScript
renders the returned state and captures input.

The baseline for both reports is commit `f7f23b3`, LM0 0.1.0, Python 3.13.9,
GCC 13.3.0, target `x86_64-linux-gnu`, Linux. Default compiler configuration was
used. The project's open and closed issues were checked through the GitHub API
on 2026-09-05; the tracker returned an empty list.

**Submission status: Not submitted.** The GitHub CLI is not installed, and no
authenticated issue-creation tool is available in this environment. These are
local reports; they have not notified maintainers through GitHub. The two report
sections below can be submitted to [Comb1e/LM Issues](https://github.com/Comb1e/LM/issues).

## Report: Missing Shared-Library Builds

Intended use: embed LM0 game logic in Python through `ctypes`, retaining its
native state between browser requests. The baseline only supported executables
and object files, so the supported CLI could not produce the needed library.

Minimal source (`examples/ffi.lm0`):

```text
module ffi version 1
export c fn @twice(%value:i64) -> i64 {
^entry:
    %two:i64 = const 2
    %result:i64 = mul %value, %two
    return %result
}
```

Reproduction command:

```sh
python3 -m lm0 build examples/ffi.lm0 --kind shared -o build/snake/libprobe.so
```

Observed exit status: 2. The diagnostic included:

```text
lm0 build: error: argument --kind: invalid choice: 'shared' (choose from exe, object)
```

Expected result: a loadable library exposing `twice`, without requiring `main`.
The workaround was to emit C and arrange a separate GCC link command manually.

Implemented improvement: `--kind shared` uses `-fPIC -shared -Wl,-z,defs`, keeps
internal LM0 symbols private, supports native link inputs/libraries, and reports
unresolved imports during the build. Sanitized shared libraries remain position
independent and can be linked to an initialized sanitizer host. Builds retain
the existing atomic artifact replacement behavior.

Validation: exports, hidden internal symbols and mutable static data at `-O0`
and `-O2`; resolved and unresolved imports; preservation of an old artifact on
failure; a sanitized native executable linked to a sanitized LM0 shared library.
Snake now builds as one native executable from four LM0 objects and the standard
archive.

## Report: No Overlap-Safe Memory Copy

Intended use: move a contiguous snake body one cell forward inside the same
allocation. The existing `copy` instruction explicitly requires disjoint ranges,
so using it for this shift would introduce undefined behavior. A descending
element-by-element load/store loop or a foreign `memmove` declaration was needed.

Minimal source:

```text
module probe version 1
fn @shift(%p:ptr<u8>, %n:u64) -> void {
^entry:
    move %p, %p, %n
    return
}
```

The baseline parser was invoked directly:

```sh
python3 -c 'from lm0 import parse; parse("module probe version 1\nfn @shift(%p:ptr<u8>, %n:u64) -> void {\n^entry:\nmove %p, %p, %n\nreturn\n}\n")'
```

Observed exit status: 1, ending in
`lm0.model.CompileError: Unknown instruction`. The raised diagnostic has code
`E_OPCODE`, phase `parse`, and actual opcode `move`.

Expected capability: an explicit byte operation with overlap-safe semantics.
Implemented improvement: `move %destination, %source, %bytes`, accepting two
pointers and a `u64` count. It lowers to `memmove`, shares memory operand
validation with `copy`, and permits null pointers when the byte count is zero.
`copy` continues to require disjoint ranges. The instruction registry, grammar,
specification, instruction reference, and LLM generation guidance describe it.

Validation: forward and backward overlap, same-address moves, null/zero count,
type and destination rejection, and sanitized `-O0`/`-O2` execution. Snake uses
`move` for both ordinary movement and growth, including the final cell of a
completed board.

## Remaining Constraints

This application uses the existing x86-64 Linux target and a native host. It
does not add WebAssembly, TLS, DNS, or a browser-native compiler backend. The
checked-in UI assets work offline while the local server is running. Native traps
still terminate the host; the socket and HTTP libraries expose bounded error
statuses rather than converting traps into exceptions.
