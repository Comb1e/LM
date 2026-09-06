# Core
1. Use Git for code management and commitment.
2. Use state machines for complex logical transitions when appropriate.
3. Keep frequently used constants in configuration files.
4. Implement repeated functionality through shared interfaces.
5. You have the autonomy to make any directional decisions regarding lm0. The target audience for this project is LLMs.

# Language Guidance

Read the [LLM generation contract](docs/llm.md) before writing LM0 programs.
Use the [language specification](docs/spec.md) and
[instruction reference](docs/instructions.md) to resolve syntax and semantics.
Build the compiler with `make` and use `build/lm0`; do not use the Python
reference frontend to compile LM0 programs.
Generate version 2 source. Prefer inferred result types and contextual literals;
retain explicit function/block interfaces. Use `build/lm0 describe OP...` for
instruction rules and `inspect --view compact` for local context. Read its
`validation` and `unresolved` fields. Pass the returned source revision to
`replace --expect-revision HASH` and inspect again after `E_STALE`.
Use `migrate INPUT -o OUTPUT` for v1 programs. Keep v1 fixtures for regression
comparisons. New compiler/evaluation code must use assembly or C, without Python.
For offline comparisons use `make eval` and the [evaluation protocol](docs/evaluation.md).
Never describe byte counts as model tokens or missing measurements as zero.

# Standard Library

Query `build/lm0 library list` and `library describe std_MODULE [std_MODULE_FUNCTION...]`
before implementing reusable utilities. Use `use std_MODULE` to import catalogue
signatures; do not copy foreign declarations or modify private handle fields.
Read [the library guide](docs/libraries.md) and the returned ownership/status
contracts. Check fallible statuses before using outputs; destroy owned handles
once and respect borrowed-view lifetimes. `make` builds and verifies the bundled
archive; `--stdlib-dir DIR` selects another matching installation.
Keep public interfaces and defaults in `stdlib/catalog.json`. Reuse the byte buffer,
checked allocator/growth, and status interfaces across implementations. Algorithms
belong in LM0 v2; platform adapters and new tooling use C or assembly, without Python.
Generated interfaces live under `build/stdlib`; update the catalogue, not those files.

# Issue Reporting

Follow the [issue-reporting workflow](docs/reporting.md) for suspected LM0 bugs,
unclear diagnostics or documentation, and missing capabilities, including
problems with known workarounds. Search for existing issues and include a minimal
reproducer, environment details, exact commands, and observed output in reports.
Record the submitted issue URL in the task summary. If submission is unavailable,
preserve a local report and state **Not submitted**, with its path and the reason.

# Validation
Run `make smoke` for every native compiler change.
Run `make eval` for evaluator changes (requires the C `json-c` development package).
Run `make test-stdlib` for catalogue, library, import, or installation changes.
Run `python3 -m unittest discover -s tests -v` before committing compiler changes.
