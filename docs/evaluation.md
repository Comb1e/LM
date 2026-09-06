# Offline V2 Evaluation

The original default export remains supported. For Python/V3 comparisons use
the schema-2 suite described below; V2 guidance now comes from `llm-v2.md`.

The C evaluator compares exact artifacts and optionally executes saved LM0
attempts. It makes no model requests and contains no tokenizer. Build from the
repository root with `make eval`; GCC and the `json-c` development package are
required. New evaluation and compiler tools contain no Python. Existing Python
tests and the legacy benchmark remain available for independent comparisons.

## Export

```sh
build/lm0-eval export build/evaluation
build/lm0-eval report build/evaluation -o build/evaluation-report.json
```

Export requires a new directory with an existing parent. `manifest.json` records
compiler identity, each artifact's kind, pair, version, filename, byte count, and
SHA-256. Numbered text files are the exact tokenizer inputs. Keep the exported
directory intact; report rejects changed or missing artifacts and changed cases.
The source filenames used by diagnostics are part of context artifacts; reuse
the same export when comparing tokenizers.

The corpus includes eight v1 examples, including the game engine, and their
validated v2 migrations. Existing v1 files remain regression baselines. Separate
artifact pairs measure source, full context, compact context, block replacement,
and a workflow comparing v1 full inspection with v2 compact inspection. Guidance
compares the archived `llm-v1.md` contract with the current v2 contract.

The 20 generation tasks and 10 repair categories mirror the legacy benchmark.
Generation prompts contain only the public example. Repair artifacts include
full-source and local-context prompts, corrected source, and valid replacement
fragments. Syntax defects fall back to full source because inspection requires
a parseable module. `evaluation-cases.json` contains the independent expected
results and array mutations; exclude it from prompts. These fixtures are public
and small, not contamination-resistant evidence of general model performance.

## Import Token Counts

Run a tokenizer independently on each numbered artifact's exact UTF-8 text.
Record its implementation/version and all settings, including special-token or
chat-envelope handling. Counts use this format:

```json
{
  "tokenizer": {"id": "chosen-tokenizer", "version": "pinned-revision", "settings": {}},
  "counts": [
    {"artifact": "add.source.v1", "sha256": "HASH_FROM_MANIFEST", "tokens": 100}
  ]
}
```

The example count is illustrative, not a measurement. Use one tokenizer
configuration per file. Duplicate or unknown artifacts, incorrect hashes,
fractional/negative counts, and missing tokenizer identity are errors.

```sh
build/lm0-eval report build/evaluation --counts counts.json -o build/report.json
```

Missing measurements are JSON `null`, including savings when either side is
missing. Negative measured savings are retained. Import validates identity and
content binding, not whether the tokenizer produced the claimed count. Byte
counts are separate observations and must never be presented as model tokens.

## Saved Attempts

Supply a JSON array at export time:

```json
[
  {
    "task": "sum",
    "version": "v2",
    "attempt": 0,
    "input": "Exact complete model input, including guidance and feedback",
    "response": "Exact raw model response",
    "source": "module candidate version 2\n..."
  }
]
```

Use generation task IDs or `repair_` plus a category from `evaluation/repairs.json`.
Attempts are unique per task/version, start at zero, and have no gaps. Preserve
the actual full input and raw response; `source` is the complete compilable
candidate reconstructed from that response, including when the model emitted a
local patch. These three texts receive separate hashed artifacts such as
`attempt.sum.v2.0.input`, `.response`, and `.source`. Token totals use input and
raw response, so reconstructed source does not understate patch efficiency.
Do not use synthetic test attempts as model-performance evidence.

```sh
build/lm0-eval export build/trial --attempts attempts.json
build/lm0-eval report build/trial --execute --counts counts.json -o build/trial-report.json
```

Every saved candidate is checked by the native compiler. Execution additionally
requires an exported `solve(ptr<i64>, u64, i64) -> i64`, no `main`, imports, or
static data. It builds a shared library and runs a separate C driver checking
all expected return values and post-call arrays. Traps, crashes, deadlines, output
limits, interface violations, and wrong answers count as failures. Without
`--execute`, correctness and attempts to functional success remain unknown.
No imported execution claims are treated as verified results.

Reports contain per-attempt validation/correctness and per-task attempts through
the first successful attempt. Token totals are complete only when both input
and response measurements exist for every counted attempt. Partial reported
sums include explicit coverage counts. Asymptotic complexity and other behavioral
task rules still need independent review; these tests establish case correctness.

## Configuration and Interpretation

`evaluation/settings.json` configures file/output limits, compilation and
execution deadlines, repair budget, native compiler, driver, and corpus paths.
Override it with `--config FILE`, or executable paths with `--compiler FILE` and
`--driver FILE`. Run from the repository root so fixture paths resolve.
The compiler itself retains its separate native configuration.

Compare the same model version, decoding settings, task inputs, trials, and
feedback policy. Count guidance, tool responses, and all repairs. Without real
token counts and model trials, the report supports source/context byte savings
and migration correctness only. The process limits are not a security sandbox;
execute trusted attempts or use external isolation, as with the legacy benchmark.

## Python/V3 comparison (schema 2)

```sh
make eval-python test-v3 test-comparison
build/lm0-eval export build/evaluation-v3 --suite python
build/lm0-eval report build/evaluation-v3 -o build/v3-report.json
```

`make eval-python` adds an optional C driver using CPython development headers
and its embedding library. Select the installation through `PYTHON_CONFIG`;
override the executable with `--python-driver FILE`. No Python implementation
of compiler, evaluator, or driver is added. Files in `evaluation/python` and
`evaluation/python.json` are comparison programs. Ordinary builds do not need
CPython. `make test-comparison` runs synthetic infrastructure/reference tests.

The suite exports 20 idiomatic Python and V3 algorithm references, ten paired
syntax/behavioral repair categories, and word-count, JSON-transform and statistics
applications in Python/V2/V3. Algorithm cases omit the extreme data row and
factorial/Fibonacci inputs whose required arithmetic overflows; retained inputs
fit signed i64 intermediates. Popcount explicitly uses a 64-bit representation.
The default suite remains the independent fixed-width wrapping regression.
Do not merge those two numeric domains into a parity claim.

Application inputs/expectations and admitted domains are in
`evaluation/applications.json`. Each case gets a fresh temporary working
directory; original checkout fixtures are not edited. Checks observe exit status,
stdout and generated JSON files. Word-count line order is irrelevant. Statistics
uses the same specified RNG in both languages, including Python's counted helper.
References are public fixtures and establish case correctness, not general model
accuracy. Binary-search complexity and other algorithm rules still require review.

Use `version` values `v1`, `v2`, `v3`, or `python`. Add a nonnegative `trial` to
separate independent trials; attempt numbering starts at zero within each
task/version/trial. Algorithm interfaces are the original V2 exported pointer,
length and key, V3 `solve(data:slice<i64>, key:i64) -> i64`, or Python
`solve(data, key)`. Application task IDs start with `app_`. Paired V3/Python repair
IDs are `repair_` plus an ID from `evaluation/comparison_repairs.json`.

Schema-2 recordings retain the original `input`, `response`, `source` fields.
For comparable model trials also record:

```json
{
  "task": "sum", "version": "v3", "language": "lm0", "language_version": "3",
  "trial": 0, "attempt": 0,
  "model": {
    "id": "actual-model", "version": "pinned-model-revision",
    "decoding": {}, "feedback_policy": "recorded-shared-policy"
  },
  "input": "Full attempt input", "response": "Raw response",
  "source": "Complete reconstructed candidate",
  "requests": [
    {"input": "Exact full input for this model request",
     "response": "Exact raw response including tool requests",
     "tools": []}
  ]
}
```

`requests` is the ordered sequence of model calls producing that attempt. Its
input must include all guidance, repeated conversation context, tool results and
feedback actually submitted on that call. Store tool exchanges in `tools` for
audit; their text is already included in subsequent full inputs and is not charged
again separately. If requests exist, counts use their `.request.N.input` and
`.request.N.response` artifacts, not the convenience attempt copies or expanded
source. Each complete recording is hashed as `.record`, binding metadata and
tool exchanges as well as candidate text. Missing request counts leave totals
unknown. Top-level `input`/`response` counts remain supported for legacy recordings.

Optional per-request `provider_usage` contains nonnegative `input_tokens` and
`output_tokens`. Reports keep these separate from externally tokenized text;
never combine provider counters and tokenizer observations into one total.
Record tokenizer special-token/chat-envelope settings in the existing count file.

Reports include per-artifact source/context/guidance measurements and V3/Python
or V3/V2 ratios wherever both counts exist. Workflow totals count requests through
first functional success, plus every attempted request in unsuccessful trials.
Language summaries report success rate and tokens spent per successful task.
Ratios require complete counts, identical task/trial coverage, and matching
model/decoding/feedback metadata. Configuration comparison uses the recorded
model JSON representation; preserve the same canonical metadata object across
languages. A parity claim additionally requires a ratio <=1 and V3 success rate
at least Python's. A ratio or success rate is null when not measurable; zero
successful tasks do not produce a finite cost-per-success value.

The evaluator validates content binding, execution and consistency; it cannot
authenticate model provenance or independently verify supplied token counts.
Without authentic recordings, this release supplies reference correctness and
artifact bytes only. Total task token savings and Python parity remain pending.
