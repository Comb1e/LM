# Offline V2 Evaluation

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
