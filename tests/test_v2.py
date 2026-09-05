"""Regression orchestration for the native v2 compiler and C evaluation tools."""

import hashlib
import json
import random
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from tests.test_assembly import NATIVE, ROOT, cli
from tests.test_assembly import native_build, run_source
from tests import test_assembly as assembly
from tests import test_native as native_reference
from tests.support import scalar_suite


def migrated_text(text):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "source.lm0"
        output = Path(directory) / "v2.lm0"
        source.write_text(text)
        code, result = cli("migrate", source, "-o", output)
        if code:
            raise AssertionError(result)
        return output.read_text()


def v2_run(source, *_options):
    return run_source(migrated_text(source))


def v2_build(module, output, **options):
    return native_build(SimpleNamespace(source=migrated_text(module.source)), output, **options)


class V2Semantics(assembly.AssemblySemantics):
    def setUp(self):
        self.enterContext(patch.object(native_reference, "run_source", v2_run))
        self.enterContext(patch.object(native_reference, "build", v2_build))

    def assert_suite(self, cases, sanitize=True):
        result = v2_run(scalar_suite(cases))
        self.assertEqual(result.exit_code, 0, result.stderr)
        self.assertEqual(result.stderr, "")


class V2Tests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory()))

    def program(self, body, declarations="", returns="i32"):
        path = self.root / "source.lm0"
        path.write_text(f"module test version 2\n{declarations}\n"
                        f"fn @main() -> {returns} {{\n^entry:\n{body}\n}}\n")
        return path

    def run_body(self, body, expected=0, declarations=""):
        code, result = cli("run", self.program(body, declarations))
        self.assertEqual(code, 0, result)
        self.assertEqual(result["exit_code"], expected, result)

    def test_contextual_literals_and_local_names(self):
        self.run_body("""jump ^loop(0)
^loop(%i:i32):
%more = lt %i, 42
branch %more, ^step(%i), ^done(%i)
^step(%i:i32):
%next = add %i, 1
jump ^loop(%next)
^done(%i:i32):
return %i""", 42)

    def test_instruction_families(self):
        self.run_body("""%p = stack i32, 2
store %p, 20
%q = offset %p, 1
store %q, 22
%a = load %p
%b = load %q
%r = add %a, %b
%bits = ptrtoint %p
%again:ptr<i32> = inttoptr %bits
%heap = alloc i32, 2
copy %heap, %p, 8
move %heap, %heap, 8
free %heap
%size = sizeof i32
%align = alignof i32
%truth = const true
%falsehood = not %truth
%n:i32 = neg -1
%f:f64 = cast 1:i32
%i:i32 = cast %f
%answer = call @identity(%r)
return %answer""", 42,
                      "fn @identity(%v:i32) -> i32 {\n^entry:\nreturn %v\n}")

    def test_inference_errors(self):
        cases = [
            ("%v = add 1, 2\nreturn 0", "E_INFER"),
            ("%v = lt 1, 2\nreturn 0", "E_INFER"),
            ("%v = cast 1:i32\nreturn 0", "E_INFER"),
            ("%v = inttoptr 0\nreturn 0", "E_INFER"),
            ("%v:i8 = add 128, 0\nreturn 0", "E_LITERAL"),
            ("%v = add 1:i32, 2:i64\nreturn 0", "E_TYPE"),
            ("%v = const 1\nreturn 0", "E_INFER"),
            ("free null\nreturn 0", "E_INFER"),
            ("%v = free null:ptr<void>\nreturn 0", "E_RESULT"),
            ("const 1\nreturn 0", "E_RESULT"),
            ("%v = add 1:i32, 2\n%v = add %v, 1\nreturn %v", "E_DUPLICATE"),
        ]
        for body, expected in cases:
            with self.subTest(body=body):
                code, result = cli("check", self.program(body))
                self.assertEqual(code, 2, result)
                self.assertEqual(result["diagnostics"][0]["code"], expected, result)

    def test_literal_trap_span(self):
        path = self.program("%v:i32 = div 1, 0\nreturn %v")
        code, result = cli("run", path)
        self.assertEqual(code, 3, result)
        self.assertEqual(result["diagnostics"][0]["span"]["line"], 5)

    def test_v1_still_rejects_compact_syntax(self):
        path = self.program("return 0")
        path.write_text(path.read_text().replace("version 2", "version 1"))
        self.assertEqual(cli("check", path)[0], 2)

    def test_migration_and_compact_context(self):
        for source in sorted((ROOT / "examples").glob("*.lm0")) + [ROOT / "examples/snake/engine.lm0"]:
            with self.subTest(source=source.name):
                output = self.root / "migrated.lm0"
                code, result = cli("migrate", source, "-o", output)
                self.assertEqual(code, 0, result)
                self.assertIn("version 2", output.read_text())
                self.assertLess(len(output.read_bytes()), len(source.read_bytes()))
                again = self.root / "again.lm0"
                self.assertEqual(cli("migrate", output, "-o", again)[0], 0)
                self.assertEqual(output.read_bytes(), again.read_bytes())
                obj = self.root / "module.o"
                self.assertEqual(cli("build", output, "--kind", "object", "-o", obj)[0], 0)
                if source.name not in {"ffi.lm0", "engine.lm0"}:
                    before = cli("run", source)
                    after = cli("run", output)
                    self.assertEqual(before[0], after[0])
                    for field in ("exit_code", "stdout", "stderr"):
                        self.assertEqual(before[1][field], after[1][field])

    def test_context_hash_and_incoming_interfaces(self):
        source = ROOT / "examples/linked_list.lm0"
        args = ["inspect", source, "--function", "sum_nodes", "--block", "loop", "--view", "compact"]
        code, context = cli(*args)
        self.assertEqual(code, 0, context)
        self.assertEqual(context["revision"], hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual(context, cli(*args)[1])
        self.assertEqual({b["name"] for b in context["incoming"]}, {"entry", "visit"})
        self.assertNotIn("instructions", context)
        self.assertTrue(context["validation"]["ok"])
        self.assertEqual(context["unresolved"], [])

    def test_invalid_context_and_revision_repair(self):
        path = self.program("%r = call @missing()\njump ^gone()")
        code, context = cli("inspect", path, "--function", "main", "--block", "entry", "--view", "compact")
        self.assertEqual(code, 0, context)
        self.assertFalse(context["validation"]["ok"])
        self.assertIn({"kind": "function", "name": "missing"}, context["unresolved"])
        self.assertIn({"kind": "block", "name": "gone"}, context["unresolved"])
        replacement = self.root / "replacement.txt"
        replacement.write_text("^entry:\nreturn 42\n")
        output = self.root / "fixed.lm0"
        args = ["replace", path, "--function", "main", "--block", "entry",
                "--replacement", replacement, "-o", output, "--expect-revision", context["revision"]]
        self.assertEqual(cli(*args)[0], 0)
        self.assertEqual(cli("run", output)[1]["exit_code"], 42)
        saved = output.read_bytes()
        path.write_text(path.read_text() + "# changed\n")
        code, result = cli(*args)
        self.assertEqual(code, 2, result)
        self.assertEqual(result["diagnostics"][0]["code"], "E_STALE")
        self.assertEqual(output.read_bytes(), saved)

    def test_describe(self):
        code, result = cli("describe", "offset", "cast", "add")
        self.assertEqual(code, 0, result)
        self.assertEqual(set(result["instructions"]), {"offset", "cast", "add"})
        self.assertEqual(cli("describe", "imaginary")[0], 2)

    def test_literal_and_scope_boundaries(self):
        self.run_body("""%a = add 18446744073709551615:u64, 1
%zero = eq %a, 0
branch %zero, ^yes(), ^bad()
^yes:
free null:ptr<void>
copy null:ptr<u8>, null:ptr<i32>, 0
%n = neg -0.0:f64
%p = ne null:ptr<i32>, null
return 0
^bad:
return 1""")
        path = self.program("jump ^next()\n^next:\nreturn %value",
                            "fn @f(%value:i32) -> i32 {\n^entry:\n%value = add 1:i32, 2\nreturn %value\n}")
        self.assertEqual(cli("check", path)[1]["diagnostics"][0]["code"], "E_DUPLICATE")
        path = self.program("%v = add 1:i32, 2\njump ^next()\n^next:\nreturn %v")
        self.assertEqual(cli("check", path)[1]["diagnostics"][0]["code"], "E_REGISTER")

    def test_revision_padding_boundaries(self):
        for length in range(40, 150):
            path = self.program("return 0")
            path.write_text(path.read_text() + "#" + "x" * length)
            code, context = cli("inspect", path, "--function", "main", "--view", "compact")
            self.assertEqual(code, 0, context)
            self.assertEqual(context["revision"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_mutated_v2_inputs_return_structured_diagnostics(self):
        source = (ROOT / "examples/v2/count.lm0").read_bytes()
        rng = random.Random(0x2A0)
        path = self.root / "mutated.lm0"
        for index in range(250):
            data = bytearray(source)
            for _ in range(rng.randrange(1, 7)):
                position = rng.randrange(len(data))
                if rng.randrange(2):
                    data[position] = rng.randrange(256)
                else:
                    del data[position]
            path.write_bytes(data)
            result = subprocess.run([str(NATIVE), "check", str(path)], capture_output=True, timeout=2)
            self.assertIn(result.returncode, (0, 2), (index, result.stderr))
            self.assertEqual(json.loads(result.stdout)["ok"], result.returncode == 0)
