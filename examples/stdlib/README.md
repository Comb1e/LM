# Standard Library Examples

Run from the repository root after `make`. All three programs use LM0 v2 and the
bundled library; no Python host or external service is involved.

```sh
build/lm0 run examples/stdlib/word_count.lm0
build/lm0 run examples/stdlib/json_transform.lm0
build/lm0 run examples/stdlib/statistics.lm0
```

- `word_count.lm0` reads `examples/stdlib/words.txt`, lowercases ASCII, and counts
  whitespace-delimited words in a map. Punctuation is part of a word. It prints
  six `word=count` lines; map order is unspecified.
- `json_transform.lm0` reads `examples/stdlib/input.json`, increments its integer
  `count` with overflow checking, and writes compact JSON to stdout and
  `build/stdlib-transformed.json`. The fixture changes 41 to 42. Output creation
  can replace that generated file; the input fixture is not modified.
- `statistics.lm0` samples 100 integers in `[0,1000)` with seed 1, computes their
  checked sum, sorts a vector, and prints `sum=47221` and `upper_median=423`.

Each program returns its library error status as its process exit code. `lm0 run`
reports that code in JSON. Input paths and seeds are fixed fixtures because the
current LM0 executable entry interface has no argument vector. Library functions
accept caller-provided spans, handles, limits and seeds for integration into
larger programs.

Use `build/lm0 library describe MODULE` for signatures, ownership, failures and
call templates. [The library guide](../../docs/libraries.md) describes the common
contract and installation; `make test-stdlib` verifies APIs and these examples.
