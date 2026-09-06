# Python comparison contract

Use the recorded CPython version and standard library. Return complete source
for generation, or the complete selected function for local repair. Define
`solve(data, key)` for algorithm tasks; return an integer and preserve the list
unless mutation is requested. Applications use the file/output requirements in
their task prompt. Include all solution-specific helpers and imports.

The ordinary algorithm suite restricts inputs so required intermediate integer
arithmetic fits signed i64. Popcount explicitly counts the 64-bit representation.
The separate legacy numeric suite includes wrapping and is not a Python parity
comparison. Follow algorithm requirements such as binary search.

Preserve the actual full model inputs, raw responses, tool exchanges and repairs.
Count this guidance whenever included in a model request. Record model version,
decoding settings, feedback policy and CPython version; do not treat synthetic
regression fixtures as model trials.
