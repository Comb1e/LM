from pathlib import Path
import tempfile

from lm0.parser import parse
from lm0.tooling import build, execute


ROOT = Path(__file__).resolve().parents[1]


def run_source(source, optimization="0", sanitize=False):
    with tempfile.TemporaryDirectory(prefix="lm0-test-") as temporary:
        program = Path(temporary) / "program"
        build(parse(source, "test.lm0"), program, optimization=optimization, sanitize=sanitize)
        return execute([str(program)], 5, 1048576)


def scalar_suite(cases):
    functions = []
    blocks = []
    for i, (type_, instructions, expected) in enumerate(cases):
        functions.append(f"fn @case_{i}() -> {type_} {{\n^entry:\n{instructions}\n    return %result\n}}\n")
        label = "entry" if i == 0 else f"test_{i}"
        next_ = f"test_{i + 1}" if i + 1 < len(cases) else "pass"
        if expected == "nan":
            comparison = f"%equal_{i}:bool = ne %actual_{i}, %actual_{i}"
        else:
            comparison = f"%expected_{i}:{type_} = const {expected}\n    %equal_{i}:bool = eq %actual_{i}, %expected_{i}"
        blocks.append(f"^{label}:\n    %actual_{i}:{type_} = call @case_{i}()\n    {comparison}\n"
                      f"    branch %equal_{i}, ^{next_}(), ^fail()\n")
    main = "fn @main() -> i32 {\n" + "".join(blocks) + """^pass:
    %success:i32 = const 0
    return %success
^fail:
    %failure:i32 = const 1
    return %failure
}
"""
    return "module numeric_tests version 1\n" + "\n".join(functions) + main
