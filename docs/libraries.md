# LM0 Standard Library

LM0 0.5 includes thirteen native modules and 114 public functions. Use the catalogue to
find an operation, obtain its exact signature and contract, then import its module:

```sh
make
build/lm0 library list
build/lm0 library describe std_bytes
build/lm0 library describe std_text std_text_parse_i64 std_text_format_i64
```

The query output is deterministic JSON. A module description includes its
dependencies, common policy, signatures, contracts and call templates. Selected
symbols use full names, optionally prefixed with `@`. Call templates assume the
typed parameters shown in the signature; they are not complete programs.
`build/stdlib/reference.md` is the generated complete reference after `make`.

| Import | Operations |
| --- | --- |
| `std_core` | Status descriptions, checked recoverable allocation, shared defaults |
| `std_bytes` | Compare/search/copy byte spans, owned growable buffers |
| `std_text` | UTF-8 validation/scalar iteration, ASCII case, decimal i64/u64 parsing/formatting |
| `std_vec` | Owned i64 vector, bounds checks, insert/remove, heapsort, lower-bound search |
| `std_map` | Copied byte-string keys and i64 values, update/delete/iteration |
| `std_json` | Strict parse, document/node access, construction/mutation, bounded serialization |
| `std_math` | Checked i64 add/subtract/multiply; f64 sqrt/sin/cos/log/exp/pow/floor/ceil |
| `std_random` | Explicit xorshift64* state, bounded draws and byte filling |
| `std_io` | File and console I/O, bounded whole-file reading, complete file writing |
| `std_time` | Monotonic/Unix nanoseconds, elapsed time, sleep |
| `std_net` | Nonblocking TCP sockets, literal-address connect/listen, polling |
| `std_process` | Native arguments, executable path, scoped shutdown signals |
| `std_http` | Incremental bounded HTTP/1 parsing and response construction |

## Generation Contract

`use std_text` is a top-level v2 declaration. It supplies all that module's exact
signatures and its transitive dependencies, once per module. Functions are ordinary
C-ABI calls named `@std_MODULE_FUNCTION`. Imports may appear before or after user
functions. Existing v1 programs and explicit C interfaces continue to work.
There are no aliases, generics, callbacks, arbitrary import paths or package downloads.

Fallible functions return `i32` statuses. Functions whose result is a value, such
as `std_bytes_compare`, `std_json_kind` and `std_vec_len`, say so explicitly.
Check status before reading a result slot; nonzero statuses do not throw or trap.

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Invalid argument or kind |
| 2 | Range or arithmetic overflow |
| 3 | Allocation failure |
| 4 | Missing key or search result |
| 5 | Invalid encoding |
| 6 | Parse error |
| 7 | I/O error |
| 8 | End of input/iteration |
| 9 | Configured or explicit limit exceeded |

Failure preserves outputs and container contents, except documented streaming I/O
progress counts. Output storage must not overlap inputs or private handle storage.
Byte spans must be initialized, live and at most `INT64_MAX` bytes; null is allowed
only for an empty span. Nonnull pointers remain the caller's responsibility.

Constructors return owned handles through output pointers. Initialize cleanup
slots to null, then call the corresponding destroy exactly once for each owned
handle. Destroy accepts null. `std_io_close` consumes a file even when closing
reports failure, and must not be retried. Library allocation failures return
status 3; the language's `alloc` instruction still traps on allocator failure.

Buffers have explicit lengths and no automatic NUL terminator. Access them with
`std_bytes_view`; its byte span may be modified in place within the existing
length. Resizing/mutating the buffer through its API invalidates prior views.
Append accepts a span of the same buffer's initialized bytes. Handle fields are
implementation details, even though LM0 exposes their layout for type checking.
Use public accessors. Only `StdRng` storage is caller-allocated, then initialized
with `std_random_seed` before use. Containers have no internal synchronization.

```text
module hello_library version 2
use std_bytes
use std_text
use std_io
fn @main() -> i32 {
^entry:
    %slot = stack ptr<StdBuf>, 1
    %data = stack ptr<u8>, 1
    %len = stack u64, 1
    %status = call @std_bytes_new(%slot)
    %ok = eq %status, 0
    branch %ok, ^format(%slot, %data, %len), ^error(%status)
^format(%slot:ptr<ptr<StdBuf>>, %data:ptr<ptr<u8>>, %len:ptr<u64>):
    %buf = load %slot
    %status = call @std_text_format_i64(42, %buf)
    %ok = eq %status, 0
    branch %ok, ^write(%buf, %data, %len), ^cleanup(%buf, %status)
^write(%buf:ptr<StdBuf>, %data:ptr<ptr<u8>>, %len:ptr<u64>):
    call @std_bytes_view(%buf, %data, %len)
    %p = load %data
    %n = load %len
    %status = call @std_io_stdout(%p, %n)
    jump ^cleanup(%buf, %status)
^cleanup(%buf:ptr<StdBuf>, %status:i32):
    call @std_bytes_destroy(%buf)
    return %status
^error(%status:i32):
    return %status
}
```

## Module Details

Text is byte-indexed UTF-8; scalar iteration validates overlong encodings,
surrogates and scalar range. ASCII transformations leave every non-ASCII byte
unchanged. Integer parsers consume complete decimal input without whitespace or
a plus sign. Signed parsing permits one minus sign. Unicode normalization,
locale-aware casing and floating-point text formatting are not provided.

Vectors contain i64 values. Sort is an in-place heapsort without callbacks or
allocation. Search requires ascending sorted data and returns the first match.
Maps use FNV-1a hashing and open addressing with copied keys, tombstones and
geometric growth. Empty keys are valid; order is unspecified. Keys returned by
iteration borrow map storage. Hashing is deterministic and not hardened against
deliberate collision workloads.

JSON documents own every node, including detached nodes and replaced roots or
members. Node handles remain valid until their document is destroyed; their
relationships can change on mutation. Retrieve byte views again after mutation.
A replacement's detached old node may be explicitly reattached. Tree attachment
requires the same owner, a detached node, and no cycle. A document's root cannot
be attached beneath another node. Arrays and objects retain insertion order;
replacing an object member retains its position.

JSON parsing and serialization use iterative state machines. Parsing requires
one complete value, rejects duplicate keys (including equivalent escapes),
invalid UTF-8, raw control characters and unpaired surrogate escapes. Strings
are decoded UTF-8; numbers retain their original JSON lexemes, including large
values and exponent notation. `std_json_i64` only converts integer lexemes, with
range checking; fraction/exponent syntax returns parse status. Kind codes are
0 null, 1 false, 2 true, 3 number, 4 string, 5 array and 6 object.

Depth counts nested containers, including empty containers. A zero depth argument
uses the catalogue default; primitive roots have depth zero. Node count includes
detached nodes. Serialization enforces an explicit byte limit and returns an
owned buffer only on success. The first release uses linear object lookup;
large objects can incur quadratic parsing work when checking duplicate keys.

Random output is deterministic, noncryptographic xorshift64*. State transitions
use shifts 12, 25, 27 and multiplier 2685821657736338717. Zero seeds map to the
catalogue's nonzero seed. Bounded sampling rejects the incomplete interval over
the generator's nonzero output domain, avoiding modulo bias. Byte filling takes
the low byte of one complete generator draw for each destination byte.

File paths are length-delimited bytes without embedded NUL; modes are 0 read,
1 create/truncate and 2 create/append. Reads retry EINTR. Complete writes retry
EINTR and report partial progress on failure, including nonblocking descriptors.
Writes suppress only their own newly generated SIGPIPE, preserving the host's
signal mask and any previously pending signal. File writes are not atomic
filesystem transactions. Whole-file limits count returned bytes, not geometric
buffer capacity. EOF is status 8 with zero bytes; a zero-capacity read succeeds.
Monotonic clocks measure durations; Unix time can move backwards. Sleep retries
interruptions. libm wrappers preserve the platform's IEEE results without
exposing errno as a second error channel.

`std_net` is deliberately nonblocking: a successful zero-byte receive/send means
would-block, while EOF and I/O errors are distinct statuses. `std_http` is an
incremental state machine; feed fragments until its ready flag is true, then
consume borrowed views before feeding again. It rejects ambiguous
Content-Length, transfer encoding, malformed CRLF, duplicate framing, and
unsupported expectations. `std_process` exposes native `argc`/`argv` and a
single signal guard whose handler only records a flag.

## Build and Repair

```sh
make stdlib
build/lm0 build program.lm0 -o build/program
build/lm0 inspect program.lm0 --function main --view compact
build/lm0 replace program.lm0 --function main --replacement main.txt --expect-revision HASH -o fixed.lm0
make test-stdlib
```

Inspection includes `library.policy`, a catalogue hash, and only the selected
unit's referenced contracts. Source revisions still hash the original file;
imports do not expand or alter its source text. Imported declarations cannot be
edited with `replace`; use `library describe` to retrieve them. Normal source
replacement and v2 migration preserve import declarations. Unknown modules and
unavailable imported source selections report `E_LIBRARY`; name conflicts report
`E_DUPLICATE`. Wrong call types/arity retain their ordinary source diagnostics.

Checking, inspection, catalogue queries, object builds and assembly emission
need only the compiler executable and native toolchain. Executable/shared builds
locate `stdlib/liblm0std.a` beside the compiler in a checkout, or
`../lib/lm0/liblm0std.a` in an installation. `--stdlib-dir DIR` overrides this
search and expects both `liblm0std.a` and `catalog.id`. A mismatch or missing
archive produces `E_LIBRARY`; a catalogue-specific linker reference also catches
stale archives copied beside a newer identity file. Such link failures retain
the normal `E_BACKEND` diagnostic. Failed builds preserve existing outputs.

`make install PREFIX=...` installs a relocatable compiler, archive, identity and
C header. `DESTDIR` supports staging. The compiler depends on libc; library
programs depend on libc/libm. `json-c` is needed to build the catalogue generator,
export checker and evaluation tools, not to run compiled LM0 programs.

For downstream linking, object/assembly command JSON includes
`link_requirements` with `archives: ["liblm0std.a"]`, `libraries: ["m"]` and
the catalogue hash. Place the archive after objects when invoking GCC:

```sh
build/lm0 build library_user.lm0 --kind object -o build/library_user.o
gcc host.c build/library_user.o build/stdlib/liblm0std.a -lm -o build/host
```

Automatic shared builds hide bundled archive symbols so separate hosts do not
accidentally interpose different library versions. User-declared C exports remain
visible. Direct GCC shared links should use `-Wl,--exclude-libs,liblm0std.a` too.
Changing the catalogue requires rebuilding the compiler, archive and importing
objects together. No third-party package manager or arbitrary module linker is
introduced in this release.
