---
name: LM0 problem or feedback
about: Report a language, compiler, tooling, or documentation problem encountered during LM0 usage.
title: "[LM0] "
---

## Summary

Describe the problem, its impact, and whether it concerns a bug, a diagnostic,
documentation, or a missing capability. Link related issues if any.

## Environment

- LM0 native compiler version and/or checkout commit:
- GCC/binutils versions and target:
- Operating system and architecture:
- Relevant configuration and compiler flags:
- Reporting agent/model and version, if known (optional):

## Reproducer

Include the smallest complete LM0 source, required inputs, and exact commands.
For documentation or capability feedback, identify the relevant page/instruction
and describe the intended operation. Mark unavailable information explicitly.

```text
Minimal LM0 source and inputs
```

```sh
Exact commands
```

## Expected Behavior

State the expected result and cite the relevant specification section when
applicable.

## Actual Behavior

Include verbatim JSON diagnostics, stdout/stderr, exit codes, and how consistently
the problem reproduces. Remove credentials and private project data.

```text
Observed output
```

## Investigation or Workaround

List any reduced reproducer, optimization comparisons, or workaround already
tried. Distinguish confirmed observations from hypotheses. Use "None" if absent.
