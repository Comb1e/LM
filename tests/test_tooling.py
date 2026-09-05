import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

from lm0.cli import main
from lm0.config import load_config
from lm0.model import CompileError
from lm0.parser import parse
from lm0.tooling import build, execute, inspect, read_module, replace
from tests.support import ROOT


class ToolingTests(unittest.TestCase):
    def cli(self, args):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = main(args)
        return code, json.loads(output.getvalue())

    def test_check_emit_build_and_run(self):
        source = str(ROOT / "examples/add.lm0")
        code, result = self.cli(["check", source])
        self.assertEqual(code, 0)
        self.assertEqual(result["functions"], 2)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code, _ = self.cli(["emit-c", source, "--entry", "-o", str(root / "module.c")])
            self.assertEqual(code, 0)
            self.assertIn("#line", (root / "module.c").read_text())
            code, _ = self.cli(["build", source, "-O", "2", "-o", str(root / "program")])
            self.assertEqual(code, 0)
            self.assertEqual(execute([str(root / "program")], 5, 10000).exit_code, 42)
        code, result = self.cli(["run", source])
        self.assertEqual(code, 0)
        self.assertEqual(result["exit_code"], 42)

    def test_missing_entry_and_invalid_source_emit_json(self):
        code, result = self.cli(["run", str(ROOT / "examples/ffi.lm0")])
        self.assertEqual(code, 2)
        self.assertEqual(result["diagnostics"][0]["code"], "E_ENTRY")
        code, result = self.cli(["check", "/no/such/lm0/source"])
        self.assertEqual(code, 2)
        self.assertFalse(result["ok"])

    def test_inspection_includes_dependencies(self):
        module = read_module(ROOT / "examples/linked_list.lm0")
        info = inspect(module, "@main", "^entry")
        self.assertEqual(info["callees"][0]["name"], "sum_nodes")
        self.assertIn("alloc", info["instructions"])
        self.assertEqual(info["types"][0]["name"], "Node")
        self.assertTrue(info["source"].startswith("^entry:"))

    def test_atomic_function_replacement_and_caller_validation(self):
        module = read_module(ROOT / "examples/add.lm0", check=False)
        replacement = "fn @add(%a:i32, %b:i32) -> i32 {\n^entry:\n%r:i32 = sub %a, %b\nreturn %r\n}\n"
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "updated.lm0"
            updated = replace(module, "add", replacement, output)
            self.assertEqual(read_module(output).function("add").blocks[0].instructions[0].op, "sub")
            original = output.read_bytes()
            with self.assertRaises(CompileError):
                replace(updated, "add", replacement.replace("i32", "i64"), output)
            self.assertEqual(output.read_bytes(), original)
            with self.assertRaises(CompileError):
                replace(updated, "add", replacement.replace("@add", "@renamed"), output)
            self.assertEqual(output.read_bytes(), original)
            with self.assertRaises(CompileError):
                replace(updated, "add", replacement + 'data @injected = "extra"\n', output)
            self.assertEqual(output.read_bytes(), original)

    def test_block_replacement_can_repair_invalid_module(self):
        source = (ROOT / "examples/add.lm0").read_text().replace("add %a, %b", "add %missing, %b")
        module = parse(source)
        replacement = "^entry:\n%answer:i32 = add %a, %b\nreturn %answer\n"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "repaired.lm0"
            replace(module, "add", replacement, path, "entry")
            read_module(path)
            code, result = self.cli(["inspect", str(path), "--function", "add", "--block", "entry"])
            self.assertEqual(code, 0)
            self.assertIn("%answer", result["source"])

    def test_failed_build_does_not_overwrite_output(self):
        source = "module m version 1\nextern c fn @missing() -> i32\nfn @main() -> i32 {\n^entry:\n%r:i32 = call @missing()\nreturn %r\n}\n"
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "program"
            output.write_bytes(b"previous artifact")
            with self.assertRaises(CompileError):
                build(parse(source), output)
            self.assertEqual(output.read_bytes(), b"previous artifact")

    def test_process_timeout_and_output_limits(self):
        timed = execute([sys.executable, "-c", "while True: pass"], 0.1, 1024)
        self.assertTrue(timed.timed_out)
        flooded = execute([sys.executable, "-c", "import os; os.write(1, b'x' * 100000)"], 5, 100)
        self.assertTrue(flooded.output_limited)
        self.assertEqual(len(flooded.stdout), 100)

    def test_configuration_and_source_limits(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "config.toml"
            path.write_text("[limits]\nsource_bytes = 10\n")
            config = load_config(str(path))
            with self.assertRaises(CompileError):
                read_module(ROOT / "examples/add.lm0", config)
            for text in ["[limits]\nsource_bytes = -1\n", "[limits]\nunknown = 2\n", "[compiler]\noptimization = 'fast'\n"]:
                path.write_text(text)
                with self.assertRaises(ValueError):
                    load_config(str(path))

    def test_runtime_trap_and_timeout_cli_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "trap.lm0"
            source.write_text("module m version 1\nfn @main() -> i32 {\n^entry:\ntrap\n}\n")
            code, result = self.cli(["run", str(source)])
            self.assertEqual(code, 3)
            self.assertEqual(result["diagnostics"][0]["code"], "E_TRAP")
            source.write_text("module m version 1\nfn @main() -> i32 {\n^entry:\njump ^loop()\n^loop:\njump ^loop()\n}\n")
            code, result = self.cli(["run", str(source), "--timeout", "0.05"])
            self.assertEqual(code, 3)
            self.assertTrue(result["timed_out"])
