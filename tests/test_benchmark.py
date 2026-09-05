import json
from pathlib import Path
import tempfile
import unittest

from lm0.benchmark import (SUM_SOURCE, TASKS, cases, execute_candidate, export_tasks,
                           grade_responses, oracle, repairs)
from lm0.config import DEFAULTS


class BenchmarkTests(unittest.TestCase):
    def test_export_counts_and_separate_evaluation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = export_tasks(root)
            self.assertEqual((result["generation_tasks"], result["repair_tasks"]), (20, 10))
            prompts = [json.loads(line) for line in (root / "tasks.jsonl").read_text().splitlines()]
            self.assertTrue(all("cases" not in p and "example" in p for p in prompts))
            self.assertEqual(len(json.loads((root / "evaluation-cases.json").read_text())), 20)
            self.assertEqual(len(repairs()), 10)

    def test_oracles_and_wraparound(self):
        for task in TASKS:
            with self.subTest(task=task):
                self.assertGreaterEqual(len(cases(task)), 6)
        self.assertEqual(oracle("sum", [(1 << 63) - 1, 1], 0), (-(1 << 63), [(1 << 63) - 1, 1]))
        self.assertEqual(oracle("prefix_sum", [1, -2, 3], 0), (2, [1, -1, 2]))
        self.assertEqual(oracle("sort", [3, -1, 3], 0), (3, [-1, 3, 3]))
        self.assertEqual(oracle("reverse", [1, 2, 3], 0), (3, [3, 2, 1]))
        self.assertEqual(oracle("popcount", [], -1), (64, []))
        self.assertEqual(oracle("binary_search", [1, 2, 2, 3], 2)[0], 1)
        self.assertEqual(oracle("factorial", [], 0)[0], 1)
        self.assertEqual(oracle("fibonacci", [], 8)[0], 21)

    def test_native_lm0_and_c_candidates(self):
        c_source = """#include <stdint.h>
#include <string.h>
int64_t solve(void *data, uint64_t n, int64_t key) {
    (void)key;
    int64_t *values = data;
    uint64_t bits = 0;
    for (uint64_t i = 0; i < n; ++i) bits += (uint64_t)values[i];
    int64_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
"""
        for language, source in [("lm0", SUM_SOURCE), ("c", c_source)]:
            with self.subTest(language=language):
                execution = execute_candidate("sum", source, language, DEFAULTS, sanitize=True)
                self.assertEqual(execution["exit_code"], 0, execution["stderr"])
                self.assertEqual(execution["results"], [case["expected"] for case in cases("sum")])

    def test_imported_results_repairs_and_tokens(self):
        rows = [
            {"task": "sum", "source": repairs()["repair_unknown_register"]["source"], "attempt": 0},
            {"task": "sum", "source": SUM_SOURCE, "attempt": 1, "tokens": {"input": 10, "output": 20},
             "execution": {"exit_code": 0, "results": [case["expected"] for case in cases("sum")]}},
            {"task": "product", "language": "c", "source": "not executed"},
        ]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "responses.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = grade_responses(path)
            summary = result["summary"]["lm0"]
            self.assertEqual(summary["tasks_repaired"], 1)
            self.assertEqual(summary["first_success_attempts"], [1])
            self.assertEqual(summary["reported_tokens"], {"input": 10, "output": 20})
            self.assertEqual(result["results"][1]["execution_origin"], "imported")
            self.assertIsNone(result["summary"]["c"]["correct_rate"])

    def test_no_invented_correctness_without_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "responses.jsonl"
            path.write_text(json.dumps({"task": "sum", "source": SUM_SOURCE}) + "\n")
            report = grade_responses(path)
            self.assertTrue(report["results"][0]["verified"])
            self.assertIsNone(report["results"][0]["correct"])
            self.assertIsNone(report["results"][0]["tokens"])

    def test_attempt_order_and_mutation_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "responses.jsonl"
            path.write_text(json.dumps({"task": "sum", "source": SUM_SOURCE, "attempt": 1}) + "\n")
            with self.assertRaises(ValueError):
                grade_responses(path)
            rows = {"task": "reverse", "language": "c", "source": "extern int placeholder;",
                    "execution": {"exit_code": 0, "results": [[len(c["data"]), c["data"]] for c in cases("reverse")]}}
            path.write_text(json.dumps(rows) + "\n")
            self.assertFalse(grade_responses(path)["results"][0]["correct"])

    def test_malformed_response_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "responses.jsonl"
            for fields in [{"task": []}, {"language": []}, {"tokens": []}, {"tokens": {"input": True}}]:
                with self.subTest(fields=fields):
                    path.write_text(json.dumps({"task": "sum", "source": SUM_SOURCE, **fields}) + "\n")
                    with self.assertRaises(ValueError):
                        grade_responses(path)
            path.write_text(json.dumps({"task": "sum", "source": SUM_SOURCE, "tokens": None,
                                        "execution": {"exit_code": False, "results": []}}) + "\n")
            self.assertEqual(grade_responses(path)["results"][0]["diagnostics"][0]["code"], "E_BENCH")
