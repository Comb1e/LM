"""Offline generation and repair tasks with independent Python result oracles."""

from collections import defaultdict
from dataclasses import asdict
import json
import math
from pathlib import Path
import tempfile

from .config import DEFAULTS
from .emit import emit_c
from .model import CompileError, Type
from .parser import parse
from .tooling import atomic_write, build_c, execute
from .verify import verify


TASKS = {
    "sum": "Return the wrapping sum of all elements; empty input returns 0.",
    "product": "Return the wrapping product of all elements; empty input returns 1.",
    "minimum": "Return the smallest signed element; empty input returns 0.",
    "maximum": "Return the largest signed element; empty input returns 0.",
    "count_equal": "Return the number of elements equal to key.",
    "index_of": "Return the first index whose element equals key, or -1 if absent.",
    "count_positive": "Return the number of strictly positive elements.",
    "is_sorted": "Return 1 if elements are in nondecreasing signed order, otherwise 0.",
    "is_palindrome": "Return 1 if elements equal their reversal, otherwise 0.",
    "sum_squares": "Return the wrapping sum of the wrapping squares of all elements.",
    "xor_reduce": "Return the bitwise XOR of all elements as i64; empty input returns 0.",
    "gcd": "Return gcd(abs(data[0]), abs(data[1])); fewer than two elements return 0. Inputs exclude INT64_MIN.",
    "factorial": "Return key! with wrapping multiplication. key is in [0, 25]. Ignore data.",
    "fibonacci": "Return Fibonacci(key), where F(0)=0 and F(1)=1, with wrapping addition. key is in [0, 100].",
    "popcount": "Return the number of set bits in the 64-bit representation of key. Ignore data.",
    "clamp_sum": "Clamp each element to [-key, key] and return their wrapping sum. key is nonnegative.",
    "binary_search": "Input is sorted. Return the first index equal to key, or -1 if absent, using binary search.",
    "reverse": "Reverse the elements in place and return n.",
    "sort": "Sort elements in nondecreasing signed order in place and return n.",
    "prefix_sum": "Replace each element by the wrapping sum through that index; return the final sum, or 0 when empty.",
}

LM0_SIGNATURE = "export c fn @solve(%data:ptr<i64>, %n:u64, %key:i64) -> i64"
C_SIGNATURE = "int64_t solve(void *data, uint64_t n, int64_t key)"

SUM_SOURCE = """module candidate version 1
export c fn @solve(%data:ptr<i64>, %n:u64, %key:i64) -> i64 {
^entry:
    %zero:u64 = const 0
    %initial:i64 = const 0
    jump ^loop(%zero, %initial)
^loop(%index:u64, %total:i64):
    %more:bool = lt %index, %n
    branch %more, ^step(%index, %total), ^done(%total)
^step(%position:u64, %accumulator:i64):
    %offset:i64 = cast %position
    %pointer:ptr<i64> = offset %data, %offset
    %value:i64 = load %pointer
    %added:i64 = add %accumulator, %value
    %one:u64 = const 1
    %next:u64 = add %position, %one
    jump ^loop(%next, %added)
^done(%result:i64):
    return %result
}
"""


def repairs() -> dict:
    changes = {
        "unknown_register": ("add %accumulator, %value", "add %missing, %value"),
        "wrong_type": ("%one:u64 = const 1", "%one:i64 = const 1"),
        "missing_terminator": ("    return %result\n", ""),
        "duplicate_register": ("%value:i64 = load %pointer", "%offset:i64 = load %pointer"),
        "branch_arity": ("^done(%total)", "^done()"),
        "literal_range": ("%one:u64 = const 1", "%one:u64 = const -1"),
        "unknown_opcode": (" = add %accumulator", " = accumulate %accumulator"),
        "pointer_index": ("offset %data, %offset", "offset %data, %position"),
        "division_by_zero": ("%added:i64 = add %accumulator, %value",
                             "%denominator:i64 = const 0\n    %added:i64 = div %value, %denominator"),
        "wrong_operation": ("%added:i64 = add %accumulator, %value", "%added:i64 = sub %accumulator, %value"),
    }
    return {"repair_" + name: {"task": "sum", "source": SUM_SOURCE.replace(old, new)}
            for name, (old, new) in changes.items()}


def wrap(value):
    value &= (1 << 64) - 1
    return value - (1 << 64) if value >= (1 << 63) else value


def oracle(task: str, data: list[int], key: int) -> tuple[int, list[int]]:
    values = list(data)
    if task == "sum":
        result = sum(values)
    elif task == "product":
        result = math.prod(values)
    elif task == "minimum":
        result = min(values, default=0)
    elif task == "maximum":
        result = max(values, default=0)
    elif task == "count_equal":
        result = values.count(key)
    elif task in {"index_of", "binary_search"}:
        result = values.index(key) if key in values else -1
    elif task == "count_positive":
        result = sum(v > 0 for v in values)
    elif task == "is_sorted":
        result = int(values == sorted(values))
    elif task == "is_palindrome":
        result = int(values == values[::-1])
    elif task == "sum_squares":
        result = sum(v * v for v in values)
    elif task == "xor_reduce":
        result = 0
        for value in values:
            result ^= value
    elif task == "gcd":
        result = math.gcd(values[0], values[1]) if len(values) >= 2 else 0
    elif task == "factorial":
        result = math.factorial(key)
    elif task == "fibonacci":
        result, next_ = 0, 1
        for _ in range(key):
            result, next_ = next_, result + next_
    elif task == "popcount":
        result = (key & ((1 << 64) - 1)).bit_count()
    elif task == "clamp_sum":
        result = sum(max(-key, min(key, v)) for v in values)
    elif task == "reverse":
        values.reverse()
        result = len(values)
    elif task == "sort":
        values.sort()
        result = len(values)
    elif task == "prefix_sum":
        result = 0
        for i, value in enumerate(values):
            result = wrap(result + value)
            values[i] = result
    else:
        raise ValueError(f"Unknown task: {task}")
    return wrap(result), values


def cases(task: str) -> list[dict]:
    inputs = [([1, 2, 3], 2), ([], 0), ([0], 0), ([-5, 0, 7, -5], -5),
              ([9, 9, 9, 9], 9), ([1, 2, 3, 2, 1], 2),
              ([-(1 << 63), (1 << 63) - 1, 1], 1), ([7, -3, 12, 0, 5, -1], 4)]
    if task == "gcd":
        inputs = [([12, 18], 0), ([], 0), ([0, 0], 0), ([-48, 18], 0), ([17, 31], 0), ([7], 0)]
    elif task in {"factorial", "fibonacci", "popcount"}:
        keys = {"factorial": [5, 0, 1, 12, 20, 25], "fibonacci": [8, 0, 1, 2, 40, 100],
                "popcount": [7, 0, 1, -1, -(1 << 63), (1 << 63) - 1]}[task]
        inputs = [([], key) for key in keys]
    elif task == "clamp_sum":
        inputs = [(data, abs(key)) for data, key in inputs]
    elif task == "binary_search":
        inputs = [(sorted(data), key) for data, key in inputs]
    return [{"data": data, "key": key, "expected": list(oracle(task, data, key))} for data, key in inputs]


def export_tasks(directory: Path) -> dict:
    prompts = []
    for name, description in TASKS.items():
        prompts.append({"id": name, "description": description,
                        "lm0_signature": LM0_SIGNATURE, "c_signature": C_SIGNATURE,
                        "constraints": "All data elements and results are signed 64-bit. n is the element count. "
                                       "Preserve input unless mutation is requested. Arithmetic wraps. "
                                       "No main, external calls, I/O, or global state. Return complete source.",
                        "example": cases(name)[0]})
    repair_prompts = [{"id": name, "task": value["task"], "description": TASKS[value["task"]],
                       "source": value["source"], "example": cases(value["task"])[0]}
                      for name, value in repairs().items()]
    atomic_write(directory / "tasks.jsonl", "".join(json.dumps(p) + "\n" for p in prompts))
    atomic_write(directory / "repairs.jsonl", "".join(json.dumps(p) + "\n" for p in repair_prompts))
    atomic_write(directory / "evaluation-cases.json", json.dumps({name: cases(name) for name in TASKS}, indent=2) + "\n")
    return {"directory": str(directory.resolve()), "generation_tasks": len(prompts), "repair_tasks": len(repair_prompts)}


def c_integer(value):
    return "INT64_MIN" if value == -(1 << 63) else f"INT64_C({value})"


def driver(task):
    lines = ["#include <stdint.h>", "#include <inttypes.h>", "#include <stdio.h>",
             "extern int64_t solve(void *, uint64_t, int64_t);", "int main(void) {"]
    for i, case in enumerate(cases(task)):
        values = case["data"]
        lines += [f"int64_t data_{i}[] = {{{', '.join(map(c_integer, values)) or '0'}}};",
                  f"int64_t result_{i} = solve(data_{i}, UINT64_C({len(values)}), {c_integer(case['key'])});",
                  f'printf("[%" PRId64 ",[", result_{i});']
        for j in range(len(values)):
            lines.append(f'printf("{"," if j else ""}%" PRId64, data_{i}[{j}]);')
        lines.append('puts("]]");')
    return "\n".join([*lines, "return 0;", "}"])


def execute_candidate(task, source, language, config, sanitize=False):
    if language == "lm0":
        module = verify(parse(source, "candidate.lm0", config), config)
        try:
            function = module.function("solve")
        except StopIteration:
            raise ValueError("Candidate must export solve")
        if (not function.exported or function.returns != Type("i64") or
                [p.type for p in function.params] != [Type("ptr", Type("i64")), Type("u64"), Type("i64")]):
            raise ValueError("Candidate solve signature does not match task")
        if any(f.external for f in module.functions):
            raise ValueError("Benchmark candidates cannot import C functions")
        source = emit_c(module, config=config)
    with tempfile.TemporaryDirectory(prefix="lm0-benchmark-") as temporary:
        root = Path(temporary)
        harness = root / "driver.c"
        harness.write_text(driver(task))
        executable = root / "candidate"
        build_c(source, executable, links=[harness], sanitize=sanitize, config=config)
        result = execute([str(executable)], config["limits"]["run_timeout_seconds"], config["limits"]["output_bytes"])
    parsed = []
    if not result.timed_out and not result.output_limited and result.exit_code == 0:
        try:
            parsed = [json.loads(line) for line in result.stdout.splitlines()]
        except ValueError:
            pass
    return {**asdict(result), "results": parsed}


def grade_responses(path: Path, execute_candidates=False, sanitize=False, config=None) -> dict:
    config = config or DEFAULTS
    rows, seen = [], set()
    repair_tasks = repairs()
    with path.open() as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            row = json.loads(line)
            if not isinstance(row, dict):
                raise ValueError(f"Response line {line_number} must be an object")
            task = row.get("task")
            language = row.get("language", "lm0")
            attempt = row.get("attempt", 0)
            if not isinstance(task, str) or (task not in TASKS and task not in repair_tasks):
                raise ValueError(f"Unknown task at response line {line_number}")
            if not isinstance(language, str) or language not in {"lm0", "c"} or (task in repair_tasks and language != "lm0"):
                raise ValueError(f"Invalid language at response line {line_number}")
            if type(attempt) is not int or not 0 <= attempt <= config["benchmark"]["max_repairs"]:
                raise ValueError("Attempt is outside configured repair budget")
            identity = task, language, attempt
            if identity in seen or (attempt and (task, language, attempt - 1) not in seen):
                raise ValueError("Attempts must be unique and sequential, starting at 0")
            seen.add(identity)
            if not isinstance(row.get("source"), str):
                raise ValueError("Each response requires a source string")
            if len(row["source"].encode("utf-8")) > config["limits"]["source_bytes"]:
                raise ValueError("Candidate source exceeds configured byte limit")
            tokens = row.get("tokens")
            if tokens is not None and not isinstance(tokens, dict):
                raise ValueError("tokens must be an object or null")
            for key, value in (tokens or {}).items():
                if key not in {"input", "output"} or type(value) is not int or value < 0:
                    raise ValueError("tokens accepts nonnegative integer input/output counts")
            rows.append({**row, "language": language, "attempt": attempt})
    results = []
    for row in rows:
        name, language, source = row["task"], row["language"], row["source"]
        task = repair_tasks[name]["task"] if name in repair_tasks else name
        item = {"task": name, "language": language, "attempt": row["attempt"],
                "characters": len(source), "tokens": row.get("tokens"),
                "parsed": None, "verified": None, "correct": None, "diagnostics": []}
        try:
            if language == "lm0":
                item["parsed"] = False
                module = parse(source, "candidate.lm0", config)
                item.update(parsed=True, verified=False)
                verify(module, config)
                item["verified"] = True
            execution = row.get("execution")
            if execute_candidates:
                execution = execute_candidate(task, source, language, config, sanitize)
            if execution is not None:
                item["execution_origin"] = "local" if execute_candidates else "imported"
                if not isinstance(execution, dict) or "results" not in execution or "exit_code" not in execution:
                    raise ValueError("execution requires results and exit_code")
                if type(execution["exit_code"]) is not int or any(
                        type(execution.get(flag, False)) is not bool for flag in ("timed_out", "output_limited")):
                    raise ValueError("execution requires an integer exit_code and Boolean status flags")
                values = execution["results"]
                if not isinstance(values, list) or any(
                        not isinstance(value, list) or len(value) != 2 or type(value[0]) is not int or
                        not isinstance(value[1], list) or any(type(v) is not int for v in value[1]) for value in values):
                    raise ValueError("results must contain [integer, integer-array] pairs")
                item["correct"] = (execution["exit_code"] == 0 and not execution.get("timed_out", False) and
                                   not execution.get("output_limited", False) and
                                   execution["results"] == [case["expected"] for case in cases(task)])
                item["execution"] = execution
        except CompileError as error:
            item["diagnostics"] = [d.json() for d in error.diagnostics]
            item["correct"] = False
        except (OSError, ValueError) as error:
            item["diagnostics"] = [{"code": "E_BENCH", "phase": "benchmark", "message": str(error)}]
            item["correct"] = False
        results.append(item)
    groups = defaultdict(list)
    for item in results:
        groups[item["task"], item["language"]].append(item)
    summary = {}
    for language in ("lm0", "c"):
        selected = [r for r in results if r["language"] == language]
        if not selected:
            continue
        task_groups = [g for (_, lang), g in groups.items() if lang == language]
        evaluated = [r for r in selected if r["correct"] is not None]
        syntax_checked = [r for r in selected if r["parsed"] is not None]
        type_checked = [r for r in selected if r["verified"] is not None]
        successes = [next((r["attempt"] for r in g if r["correct"]), None) for g in task_groups]
        summary[language] = {
            "tasks": len(task_groups), "attempts": len(selected), "evaluated_attempts": len(evaluated),
            "parse_rate": sum(r["parsed"] for r in syntax_checked) / len(syntax_checked) if syntax_checked else None,
            "verify_rate": sum(r["verified"] for r in type_checked) / len(type_checked) if type_checked else None,
            "correct_rate": sum(r["correct"] for r in evaluated) / len(evaluated) if evaluated else None,
            "first_attempt_successes": sum(r["attempt"] == 0 and r["correct"] is True for r in selected),
            "tasks_solved": sum(attempt is not None for attempt in successes),
            "tasks_repaired": sum(attempt is not None and attempt > 0 for attempt in successes),
            "first_success_attempts": successes,
            "tokens_reported_attempts": sum(r["tokens"] is not None for r in selected),
            "reported_tokens": {key: sum((r["tokens"] or {}).get(key, 0) for r in selected) for key in ("input", "output")},
        }
    return {"summary": summary, "results": results}
