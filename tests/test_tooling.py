import contextlib
import ctypes
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

    def test_shared_library_cli_exports_and_private_symbols(self):
        source = """module shared version 1
data @count = "\\u0000"
fn @private() -> u8 {
^entry:
    %p:ptr<u8> = address @count
    %old:u8 = load %p
    %one:u8 = const 1
    %next:u8 = add %old, %one
    store %p, %next
    return %next
}
export c fn @next_count() -> u8 {
^entry:
    %result:u8 = call @private()
    return %result
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "shared.lm0"
            path.write_text(source)
            for optimization in ("0", "2"):
                library = root / f"shared{optimization}.so"
                code, result = self.cli(["build", str(path), "--kind", "shared", "-O", optimization, "-o", str(library)])
                self.assertEqual(code, 0, result)
                self.assertEqual(result["kind"], "shared")
                loaded = ctypes.CDLL(str(library))
                loaded.next_count.argtypes = []
                loaded.next_count.restype = ctypes.c_uint8
                self.assertEqual([loaded.next_count(), loaded.next_count()], [1, 2])
                with self.assertRaises(AttributeError):
                    loaded.lm0_fn_private
                with self.assertRaises(AttributeError):
                    loaded.lm0_data_count

    def test_shared_library_link_inputs_and_unresolved_imports(self):
        source = """module foreign version 1
extern c fn @foreign(%value:i32) -> i32
export c fn @public_call(%value:i32) -> i32 {
^entry:
    %result:i32 = call @foreign(%value)
    return %result
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "foreign.so"
            library.write_bytes(b"previous artifact")
            with self.assertRaises(CompileError):
                build(parse(source), library, kind="shared")
            self.assertEqual(library.read_bytes(), b"previous artifact")
            helper = root / "foreign.c"
            helper.write_text("#include <stdint.h>\nint32_t foreign(int32_t v) { return v + 1; }\n")
            build(parse(source), library, kind="shared", links=[helper])
            loaded = ctypes.CDLL(str(library))
            loaded.public_call.argtypes = [ctypes.c_int32]
            loaded.public_call.restype = ctypes.c_int32
            self.assertEqual(loaded.public_call(41), 42)

    def test_sanitized_shared_library_in_native_host(self):
        from lm0.tooling import build_c
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "ffi.so"
            build(read_module(ROOT / "examples/ffi.lm0"), library, kind="shared", sanitize=True)
            executable = root / "host"
            build_c((ROOT / "examples/ffi_driver.c").read_text(), executable,
                    links=[library], sanitize=True)
            result = execute([str(executable)], 5, 10000)
            self.assertEqual(result.exit_code, 0, result.stderr)
            self.assertEqual(result.stdout, "42\n")
            self.assertEqual(result.stderr, "")
