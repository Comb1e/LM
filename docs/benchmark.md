# Offline Benchmark Protocol

The benchmark tests generation and repair, without contacting a model service.
Export prompts and separate evaluation cases:

```sh
python3 -m lm0 bench export build/benchmark
```

- `tasks.jsonl`: 20 programming tasks, paired LM0/C signatures, constraints, and
  one public example each.
- `repairs.jsonl`: 10 defective LM0 sum implementations. Eight contain parse/type/
  scope/control-flow defects, one divides by zero, and one computes the wrong sum.
- `evaluation-cases.json`: All correctness cases, including boundary values,
  empty arrays, negatives, duplicates, and wrapping arithmetic. Keep this file
  out of the model prompt. The cases are public repository fixtures, not a secure
  or contamination-resistant benchmark.

Each candidate exports `solve(data, n, key) -> i64`. LM0 uses
`export c fn @solve(%data:ptr<i64>, %n:u64, %key:i64) -> i64`.
C uses `int64_t solve(void *data, uint64_t n, int64_t key)` and includes its own
standard headers. The driver supplies initialized arrays and checks the return
value and the entire array after each call. Tasks specify when mutation is
required. No main, external effects, global state, or input/output are allowed
by the task protocol. LM0 imports are rejected by local execution; C task-rule
compliance and asymptotic complexity require independent review.

## Response Format

Supply one JSON object per line:

```json
{"task":"sum","language":"lm0","attempt":0,"source":"module candidate version 1\n...","tokens":{"input":100,"output":200}}
```

`task` is a generation ID or repair ID from the exported files. `language`
defaults to `lm0`; C uses `c`. Repair fixtures are LM0-only. Attempts are unique
per task/language and start at 0, followed by 1, 2, and 3 when repairs are needed.
The configured `benchmark.max_repairs` controls that upper bound. Supply complete
source for every attempt, even when the LLM originally produced a local patch.

Token counts are optional actual model/tokenizer measurements, with nonnegative
integer `input` and `output` fields. Characters are reported separately. Missing
token counts remain unknown; reported totals cover only supplied measurements.

Precomputed execution results can be imported in each record:

```json
{"execution":{"exit_code":0,"results":[[6,[1,2,3]],[0,[]]],"timed_out":false,"output_limited":false}}
```

The abbreviated example above shows the shape, not the complete sum results.
`results` must contain `[return_value, post_call_array]` for every evaluation
case in order. Imported results are trusted external observations, not proof
that the submitted source produced them; reports label their origin.

```sh
python3 -m lm0 bench grade responses.jsonl -o build/report.json
python3 -m lm0 bench grade responses.jsonl --execute --sanitize -o build/report.json
```

Without `--execute`, LM0 source is parsed and verified, and imported results are
graded if present. Without execution evidence, functional correctness is null.
C parsing/verification rates remain null: GCC's full compilation is checked only
during local execution, and syntax is not separated from semantic errors.

With `--execute`, saved candidates are compiled and run against a native driver.
Local execution replaces any imported result. Execution is bounded by configured
compiler/run deadlines and output size, but candidate machine code has native
process privileges. Use an external isolation environment for untrusted input.
Sanitizers are optional for this command and enabled in the compiler tests.

## Metrics and Comparison

The JSON report includes per-attempt parse/verification/correctness status,
diagnostics, execution origin, and supplied tokens. Per-language summaries
include first-attempt successes, tasks solved, tasks repaired after attempt 0,
attempts to first success, and rates with their applicable denominators.
Null correctness is excluded from correctness-rate calculations. Verified LM0
programs can still fail functional tests or access memory incorrectly.

For an empirical comparison, use the same model version, task inputs, decoding
settings, generation/repair budgets, and independent trials for LM0 and C. Give
LM0 the compact language sheet and count that context in input tokens. Apply
equivalent execution feedback and sanitizer policy. Keep generation, syntax
repair, and algorithm repair results distinguishable by task IDs. Preserve
original responses and model metadata alongside the report.

These 20 tasks are a small evaluation fixture, not evidence of general LLM
superiority. No real model responses or claimed performance improvement ship
with the prototype. The Python result oracles are independent of LM0 execution;
native LM0 and C sum candidates exercise the complete grading path in tests.
