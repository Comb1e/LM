"""Regression tests only; the evaluator itself and every compile step are native."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from lm0.benchmark import cases as reference_cases
from tests.test_assembly import ROOT, cli

EVAL = ROOT / "build/lm0-eval"


class EvaluationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "eval"], cwd=ROOT, check=True, capture_output=True)
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.directory = cls.root / "export"
        v1 = (ROOT / "evaluation/sum.v1.lm0").read_text()
        migrated = cls.root / "sum.lm0"
        assert cli("migrate", ROOT / "evaluation/sum.v1.lm0", "-o", migrated)[0] == 0
        v2 = migrated.read_text()
        records = []
        for version, number, source in [
            ("v1", 0, v1), ("v2", 0, v2.replace("add %accumulator", "wrong %accumulator")),
            ("v2", 1, v2.replace("add %accumulator", "sub %accumulator")), ("v2", 2, v2),
        ]:
            records.append({"task": "sum", "version": version, "attempt": number,
                            "input": f"Synthetic regression input {version} {number}",
                            "response": "```text\n" + source + "```", "source": source})
        cls.attempts = cls.root / "attempts.json"
        cls.attempts.write_text(json.dumps(records))
        result = subprocess.run([str(EVAL), "export", str(cls.directory), "--attempts", str(cls.attempts)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        cls.manifest = json.loads((cls.directory / "manifest.json").read_text())
        cls.index = {item["id"]: item for item in cls.manifest["artifacts"]}

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def evaluate(self, *options, directory=None):
        result = subprocess.run([str(EVAL), "report", str(directory or self.directory), *map(str, options)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        return result.returncode, json.loads(result.stdout)

    def test_native_oracles_match_reference_cases(self):
        actual = json.loads((self.directory / "evaluation-cases.json").read_text())
        self.assertEqual(len(actual), 20)
        for task, fixtures in actual.items():
            with self.subTest(task=task):
                self.assertEqual(fixtures, reference_cases(task))

    def test_artifact_hashes_and_unknown_tokens(self):
        for artifact in self.manifest["artifacts"]:
            data = (self.directory / artifact["file"]).read_bytes()
            self.assertEqual(len(data), artifact["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), artifact["sha256"])
        code, report = self.evaluate()
        self.assertEqual(code, 0, report)
        self.assertTrue(all(pair["tokens_saved"] is None for pair in report["pairs"]))
        self.assertTrue(all(row["correct"] is None for row in report["attempts"]))
        self.assertIsNone(report["tasks"]["sum.v2"]["attempts_to_success"])

    def test_execute_saved_attempts_and_count_import(self):
        counts = {"tokenizer": {"id": "synthetic-unit-test", "version": "1", "settings": {}}, "counts": []}
        for name, item in self.index.items():
            if name.startswith("attempt."):
                counts["counts"].append({"artifact": name, "sha256": item["sha256"], "tokens": 100})
        path = self.root / "counts.json"
        path.write_text(json.dumps(counts))
        code, report = self.evaluate("--execute", "--counts", path)
        self.assertEqual(code, 0, report)
        self.assertEqual([row["correct"] for row in report["attempts"]], [True, False, False, True])
        group = report["tasks"]["sum.v2"]
        self.assertEqual(group["attempts_to_success"], 3)
        self.assertEqual(group["total_input_tokens"], 300)
        self.assertEqual(group["total_output_tokens"], 300)
        self.assertEqual(group["measured_attempts"], 3)

    def test_reject_invalid_counts_and_changed_artifacts(self):
        item = self.index["add.source.v1"]
        base = {"artifact": item["id"], "sha256": item["sha256"], "tokens": 1}
        variants = [dict(base, sha256="0" * 64), dict(base, artifact="missing"),
                    dict(base, tokens=-1), dict(base, tokens=1.5), dict(base, tokens=2**64-1)]
        path = self.root / "bad-counts.json"
        for record in variants:
            path.write_text(json.dumps({"tokenizer": {"id": "test", "version": "1", "settings": {}}, "counts": [record]}))
            self.assertEqual(self.evaluate("--counts", path)[0], 2)
        path.write_text(json.dumps({"tokenizer": {"id": "test", "version": "1", "settings": {}}, "counts": [base, base]}))
        self.assertEqual(self.evaluate("--counts", path)[0], 2)
        file = self.directory / item["file"]
        original = file.read_bytes()
        try:
            file.write_bytes(original + b"\n")
            self.assertEqual(self.evaluate()[0], 2)
        finally:
            file.write_bytes(original)

    def test_partial_measurements_are_not_complete_totals(self):
        name = "attempt.sum.v2.0.input"
        path = self.root / "partial.json"
        path.write_text(json.dumps({"tokenizer": {"id": "synthetic-unit-test", "version": "1", "settings": {}},
                                  "counts": [{"artifact": name, "sha256": self.index[name]["sha256"], "tokens": 17}]}))
        code, report = self.evaluate("--counts", path)
        self.assertEqual(code, 0, report)
        group = report["tasks"]["sum.v2"]
        self.assertEqual(group["reported_input_tokens"], 17)
        self.assertIsNone(group["total_input_tokens"])
        self.assertIsNone(group["total_output_tokens"])

    def test_native_execution_deadline(self):
        loop = ("module endless version 2\n"
                "export c fn @solve(%data:ptr<i64>, %n:u64, %key:i64) -> i64 {\n"
                "^entry:\njump ^loop()\n^loop:\njump ^loop()\n}\n")
        path = self.root / "endless.json"
        path.write_text(json.dumps([{"task": "product", "version": "v2", "attempt": 0,
                                     "input": "Synthetic timeout test", "response": loop, "source": loop}]))
        directory = self.root / "timeout-export"
        result = subprocess.run([str(EVAL), "export", str(directory), "--attempts", str(path)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout)
        settings = json.loads((ROOT / "evaluation/settings.json").read_text())
        settings["run_timeout_seconds"] = 1
        config = self.root / "timeout-settings.json"
        config.write_text(json.dumps(settings))
        code, report = self.evaluate("--execute", "--config", config, directory=directory)
        self.assertEqual(code, 0, report)
        row = report["attempts"][0]
        self.assertFalse(row["correct"])
        self.assertTrue(row["timed_out"])
        self.assertIsNone(report["tasks"]["product.v2"]["attempts_to_success"])
