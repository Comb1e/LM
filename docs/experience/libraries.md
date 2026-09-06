# Standard Library Application Report

The first library release adds ten modules and 86 public functions, with LM0 v2
implementations for containers, text algorithms, checked arithmetic, random
generation, JSON, elapsed time and whole-file workflows. C adapters provide
recoverable allocation, libm and POSIX services. The compiler remains assembly.

## Source and Context Measurements

The C measurement tool constructs a minimal manual-interface baseline for each
example using verified inspection metadata. It retains original bodies and data,
removes imports, and supplies only the needed exact structs and foreign
declarations. Both variants pass native checking. All artifact bytes, hashes,
and unmeasured token fields are exported for inspection.

| Example | With imports | Manual declarations | Source reduction |
| --- | ---: | ---: | ---: |
| Word count | 5,505 bytes | 6,716 bytes | 18.0% |
| JSON transformer | 5,388 bytes | 7,093 bytes | 24.0% |
| Statistics | 4,399 bytes | 5,530 bytes | 20.5% |
| Total | 15,292 bytes | 19,339 bytes | 20.9% |

Compact `main` context totals 22,149 bytes with imports, versus 14,892 bytes for
raw foreign declarations. The imported version also supplies ownership and error
contracts; adding identical guidance to the manual context gives the same
22,149-byte total. This release reduces source declarations and automates access
to contracts; it does not demonstrate smaller context when information is equal.
No tokenizer counts or model trials were collected. All token metrics are null.

Reproduce into a new directory:

```sh
make build/library-measure
build/library-measure build/lm0 build/stdlib-comparison-v0.4 examples/stdlib/word_count.lm0 examples/stdlib/json_transform.lm0 examples/stdlib/statistics.lm0
```

The directory contains exact import/manual source, raw compact contexts, manual
contexts enriched with identical contracts, and `report.json`. JSON contexts use
one common serializer. The tool supports these examples without local struct
declarations; it verifies every generated baseline and does not call a model.
The [recorded report](../../evaluation/stdlib-report.json) contains the measured
rows and hashes. Native integration tests also execute each manual baseline and
compare its result/output with the corresponding imported program.

## Validation

The native C suite checks all public APIs, signed integer boundaries, UTF-8
validation, source imports and duplicate/type diagnostics, vector ordering,
map growth/deletion/iteration, JSON round trips and malformed inputs, deterministic
random output and rejection sampling, file limits/EOF/partial writes/broken pipes,
clock behavior, and injected allocation failure with tracked cleanup. JSON depth
tests reach 1,024 containers without recursive parsing or serialization. Five
hundred deterministic JSON mutations exercise bounded error paths.

Compiler integration tests cover compact contracts, revisions, replacement,
migration, executable/object/shared linking, hidden archive symbols, standalone
checking without an archive, explicit archive selection, installation relocation,
catalogue mismatch rejection, and all three example outputs. The export checker
compares LM0 signatures against the catalogue and accounts for every archive
function; C adapters compile against the generated header.

Release checks passed on GCC 13.3.0, binutils 2.42, `x86_64-linux-gnu`:

```sh
make smoke eval test-stdlib
python3 -m unittest discover -s tests -v
```

All 118 existing regression tests passed. The compiler, catalogue tools, library,
evaluator, smoke tests, and native library tests also passed a forced rebuild in
an empty environment whose PATH contained only native tools (`gcc`, `as`, `ld`,
`mkdir`, `mktemp`, `rm`, `sh`, `ar`, `mv`, `touch`, `nm`, `make`, `install`).
Python was unavailable on that PATH. `readelf -d build/lm0` reports only libc as
a compiler dependency.

## Local Capability Report

**Not submitted.** The GitHub CLI is not installed and no authenticated issue
submission connector is available. This document is the preserved local report;
it has not notified maintainers externally. The repository's open and closed
issues were checked through the GitHub API on 2026-09-06; the response was `[]`.
It can be submitted to [Comb1e/LM Issues](https://github.com/Comb1e/LM/issues).

Baseline: commit `caa6355`, LM0 0.3.0 native x86-64 Linux, default configuration.
The baseline specification documents C FFI and explicitly has no module linker.
It provides no bundled standard-library interfaces or discoverable library
contracts. A runnable reproducer is not necessary to establish this missing
capability; the intended operation is:

```text
module library_user version 2
use std_vec
fn @size(%vec:ptr<StdVec>) -> u64 {
^entry:
    %n = call @std_vec_len(%vec)
    return %n
}
```

Intended commands are `lm0 check library_user.lm0` and
`lm0 library describe std_vec std_vec_len`. Before this release callers had to
create their own containers, repeat exact foreign signatures and layouts, and
arrange linking. Inspection could supply a declared C signature but had no
library-specific ownership or failure rules. Recoverable allocation required
manual FFI because the language's `alloc` traps on failure.

Implemented response: bundled catalogue imports, generated ABI declarations,
native linking/installation, compact per-call contracts, common recoverable
allocation and status rules, and ten modules built on shared primitives. The
language's existing raw-memory and numeric semantics remain unchanged.

Remaining limits are explicit: i64 vector specialization, byte-key/i64 maps,
linear JSON object lookup, manual cleanup, no float text conversion, no Unicode
normalization or locale casing, and no networking/concurrency/package downloads.
These bound the first release's library collection; they are not compiler bugs
or evidence of measured LLM task-success improvements.

## Validation Fixture Report

**Not submitted**, for the submission limitation above. The first full regression
run failed `test_session_isolation_settings_expiry_and_capacity` at its expiry
assertion: `AssertionError: KeyError not raised`. That existing test set
`session.touched=0`, assuming system uptime exceeded the configured 3,600-second
TTL. `/proc/uptime` reported 2,218.52 seconds when investigated, so zero was not
an expired timestamp. The reproducer is the existing test on a recently started
machine:

```sh
python3 -m unittest tests.test_snake.SnakeTests.test_session_isolation_settings_expiry_and_capacity -v
```

Commit `52907ba` sets the fixture's timestamp to its recorded touch time minus
the TTL minus one second. This removes the uptime assumption without changing
the host's expiry behavior. The focused test and subsequent complete 118-test
run passed.
