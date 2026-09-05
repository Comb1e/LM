"""Native compiler acceptance tests; Python orchestrates but never compiles LM0."""

import ctypes
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from lm0.model import CompileError, Diagnostic
from lm0.tooling import ProcessResult
from tests import test_frontend as frontend_reference
from tests import test_native as native_reference
from tests.support import ROOT, scalar_suite


NATIVE = ROOT / "build/lm0"


def cli(*args):
    result = subprocess.run([str(NATIVE), *map(str, args)], capture_output=True, text=True, timeout=15)
    return result.returncode, json.loads(result.stdout)


def native_build(module, output, *, kind="exe", links=(), libraries=(), **_reference_options):
    # Existing semantic fixtures request reference backend optimizations and
    # instrumentation. Their native counterparts exercise only documented -O0.
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "input.lm0"
        source.write_text(module.source)
        args = ["build", source, "-o", output, "--kind", kind]
        for link in links:
            args += ["--link", link]
        for library in libraries:
            args += ["--library", library]
        code, result = cli(*args)
        if code:
            raise CompileError([Diagnostic(d["code"], d["phase"], d["message"])
                                for d in result["diagnostics"]])
        return result


def run_source(source, *_reference_options):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "input.lm0"
        path.write_text(source)
        code, result = cli("run", path)
        if code == 2:
            raise AssertionError(result)
        return ProcessResult(**{key: result[key] for key in
                                ("exit_code", "stdout", "stderr", "timed_out", "output_limited")})


class AssemblyFrontend(frontend_reference.FrontendTests):
    def rejected(self, source, code):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.lm0"
            path.write_text(source)
            status, result = cli("check", path)
        self.assertEqual(status, 2, result)
        self.assertIn(code, [d["code"] for d in result["diagnostics"]], result)
        from lm0.model import Span
        diagnostics = []
        for item in result["diagnostics"]:
            item = dict(item)
            if "span" in item:
                item["span"]["file"] = "input.lm0"
                item["span"] = Span(**item["span"])
            diagnostics.append(Diagnostic(**item))
        return diagnostics


class AssemblySemantics(native_reference.NativeTests):
    def setUp(self):
        self.enterContext(patch.object(native_reference, "run_source", run_source))
        self.enterContext(patch.object(native_reference, "build", native_build))

    def assert_suite(self, cases, sanitize=True):
        result = run_source(scalar_suite(cases))
        self.assertEqual(result.exit_code, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_sanitizer_detects_use_after_free(self):
        for option in ("--sanitize", "-O1", "-O2", "-O3", "-Os"):
            code, result = cli("run", ROOT / "examples/add.lm0", option)
            self.assertEqual(code, 2)
            self.assertEqual(result["diagnostics"][0]["code"], "E_UNSUPPORTED")


class AssemblyTools(unittest.TestCase):
    def setUp(self):
        self.directory = self.enterContext(tempfile.TemporaryDirectory())
        self.root = Path(self.directory)

    def source(self, body, returns="i32"):
        path = self.root / "input.lm0"
        path.write_text(f"module test version 1\nfn @main() -> {returns} {{\n^entry:\n{body}\n}}\n")
        return path

    def test_malformed_input_stays_bounded_and_structured(self):
        baseline = (ROOT / "examples/add.lm0").read_bytes()
        generator = random.Random(0x1A0)
        path = self.root / "mutated.lm0"
        for index in range(200):
            data = bytearray(baseline)
            for _ in range(generator.randrange(1, 7)):
                operation = generator.randrange(3)
                if operation == 0 and data:
                    data[generator.randrange(len(data))] = generator.randrange(256)
                elif operation == 1 and data:
                    del data[generator.randrange(len(data))]
                else:
                    data.insert(generator.randrange(len(data) + 1), generator.randrange(256))
            path.write_bytes(data)
            result = subprocess.run([str(NATIVE), "check", str(path)], capture_output=True, timeout=2)
            self.assertIn(result.returncode, (0, 2), (index, result.returncode, result.stderr))
            message = json.loads(result.stdout.decode("ascii"))
            self.assertEqual(message["ok"], result.returncode == 0)
            if result.returncode:
                self.assertTrue(message["diagnostics"])

    def test_emit_and_direct_assembly(self):
        assembly = self.root / "module.s"
        code, result = cli("emit-asm", ROOT / "examples/add.lm0", "--entry", "-o", assembly)
        self.assertEqual(code, 0, result)
        self.assertIn(".loc 1", assembly.read_text())
        executable = self.root / "program"
        subprocess.run(["gcc", str(assembly), "-o", str(executable)], check=True, capture_output=True)
        self.assertEqual(subprocess.run([str(executable)]).returncode, 42)

    def test_atomic_repair_and_inspection(self):
        source = self.root / "source.lm0"
        source.write_text((ROOT / "examples/add.lm0").read_text().replace("add %a, %b", "add %missing, %b"))
        fragment = self.root / "fragment.txt"
        fragment.write_text("^entry:\n%answer:i32 = add %a, %b\nreturn %answer\n")
        output = self.root / "nested directory" / "fixed.lm0"
        args = ["replace", source, "--function", "add", "--block", "entry", "--replacement", fragment, "-o", output]
        code, result = cli(*args)
        self.assertEqual(code, 0, result)
        code, result = cli("inspect", output, "--function", "@add", "--block", "^entry")
        self.assertEqual(code, 0, result)
        self.assertIn("%answer", result["source"])
        original = output.read_bytes()
        for text in ("^entry:\nreturn\n", "^entry:\ntrap\n^extra:\ntrap\n"):
            fragment.write_text(text)
            self.assertEqual(cli(*args)[0], 2)
            self.assertEqual(output.read_bytes(), original)

    def test_function_replacement_and_rejected_injection(self):
        source = ROOT / "examples/add.lm0"
        fragment = self.root / "fragment.txt"
        text = "fn @add(%a:i32, %b:i32) -> i32 {\n^entry:\n%r:i32 = sub %a, %b\nreturn %r\n}\n"
        fragment.write_text(text)
        output = self.root / "fixed.lm0"
        args = ["replace", source, "--function", "add", "--replacement", fragment, "-o", output]
        self.assertEqual(cli(*args)[0], 0)
        self.assertEqual(cli("run", output)[1]["exit_code"], 254)
        original = output.read_bytes()
        for invalid in (text.replace("i32", "i64"), text.replace("@add", "@other"), text + 'data @injected = "x"\n'):
            fragment.write_text(invalid)
            self.assertEqual(cli(*args)[0], 2)
            self.assertEqual(output.read_bytes(), original)

    def test_module_inspection_and_dependencies(self):
        code, result = cli("inspect", ROOT / "examples/linked_list.lm0", "--module")
        self.assertEqual(code, 0, result)
        self.assertEqual([f["name"] for f in result["functions"]], ["sum_nodes", "main"])
        code, result = cli("inspect", ROOT / "examples/linked_list.lm0", "--function", "main", "--block", "entry")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["types"][0]["name"], "Node")
        self.assertEqual(result["callees"][0]["name"], "sum_nodes")

    def test_config_limits_and_timeouts(self):
        config = self.root / "settings.toml"
        config.write_text("[limits]\nsource_bytes = 10\n")
        code, result = cli("--config", config, "check", ROOT / "examples/add.lm0")
        self.assertEqual(code, 2)
        self.assertEqual(result["diagnostics"][0]["code"], "E_LIMIT")
        for text in ("[limits]\nunknown=2\n", "[limits]\nsource_bytes=-1\n", "[bogus]\n", "[compiler]\noptimization='2'\n"):
            config.write_text(text)
            self.assertEqual(cli("--config", config, "check", ROOT / "examples/add.lm0")[0], 2)
        path = self.source("jump ^loop()\n^loop:\njump ^loop()")
        code, result = cli("run", path, "--timeout", "0.05")
        self.assertEqual(code, 3, result)
        self.assertTrue(result["timed_out"])
        self.assertLess(result["exit_code"], 0)

    def test_runtime_diagnostic_and_output_budget(self):
        code, result = cli("run", self.source("trap"))
        self.assertEqual(code, 3, result)
        self.assertEqual(result["diagnostics"][0]["code"], "E_TRAP")
        self.assertEqual(result["diagnostics"][0]["span"]["line"], 4)
        config = self.root / "settings.toml"
        config.write_text("[limits]\noutput_bytes = 64\n")
        path = self.root / "flood.lm0"
        path.write_text('module flood version 1\ndata @text = "0123456789\\u0000"\nextern c fn @puts(%p:ptr<u8>) -> i32\nfn @main() -> i32 {\n^entry:\njump ^loop()\n^loop:\n%p:ptr<u8> = address @text\n%n:i32 = call @puts(%p)\njump ^loop()\n}\n')
        code, result = cli("--config", config, "run", path)
        self.assertEqual(code, 3, result)
        self.assertTrue(result["output_limited"])
        self.assertEqual(len(result["stdout"]) + len(result["stderr"]), 64)

    def test_invalid_utf8_output_and_source_are_json_safe(self):
        path = self.source("""%byte:ptr<u8> = stack u8, 1
%value:u8 = const 255
store %byte, %value
%fd:i32 = const 1
%count:u64 = const 1
%written:i64 = call @write(%fd, %byte, %count)
%zero:i32 = const 0
return %zero""")
        text = path.read_text().replace("fn @main", "extern c fn @write(%fd:i32, %buffer:ptr<u8>, %count:u64) -> i64\nfn @main")
        path.write_text(text)
        code, result = cli("run", path)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["stdout"], "\ufffd")
        path.write_bytes(b"module invalid version 1\n\xff")
        code, result = cli("check", path)
        self.assertEqual(code, 2)
        self.assertEqual(result["diagnostics"][0]["code"], "E_SYNTAX")

    def test_unresolved_shared_import_is_atomic(self):
        path = self.root / "shared.lm0"
        path.write_text("module m version 1\nextern c fn @missing() -> i32\nexport c fn @public_call() -> i32 {\n^entry:\n%r:i32 = call @missing()\nreturn %r\n}\n")
        output = self.root / "shared.so"
        output.write_bytes(b"original")
        code, result = cli("build", path, "--kind", "shared", "-o", output)
        self.assertEqual(code, 2, result)
        self.assertEqual(output.read_bytes(), b"original")
        helper = self.root / "helper.c"
        helper.write_text("int missing(void) { return 42; }\n")
        code, result = cli("build", path, "--kind", "shared", "-o", output, "--link", helper)
        self.assertEqual(code, 0, result)
        self.assertEqual(ctypes.CDLL(str(output)).public_call(), 42)

    def test_unicode_strings_and_empty_data(self):
        path = self.root / "unicode.lm0"
        path.write_text('module m version 1\ndata @text = "A\\uD83D\\uDE00\\u0000"\ndata @empty = ""\nextern c fn @puts(%p:ptr<u8>) -> i32\nfn @main() -> i32 {\n^entry:\n%p:ptr<u8> = address @text\n%r:i32 = call @puts(%p)\nreturn %r\n}\n')
        code, result = cli("run", path)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["stdout"], "A\U0001f600\n")
        for bad in ('"\\uD800"', '"\\uDC00"', '"\\q"'):
            path.write_text(f"module m version 1\ndata @x = {bad}\n")
            self.assertEqual(cli("check", path)[0], 2)

    def test_float_boundaries_and_nan_comparisons(self):
        cases = []
        for type_ in ("f32", "f64"):
            for op in ("eq", "ne", "lt", "le", "gt", "ge"):
                cases.append(("bool", f"%a:{type_} = const nan\n%b:{type_} = const 1\n%result:bool = {op} %a, %b", "true" if op == "ne" else "false"))
        for type_, value, expected in [("i8", "-128.9", "-128"), ("i8", "127.9", "127"), ("u8", "255.9", "255"), ("u32", "4294967295.9", "4294967295"), ("u64", "18446744073709549568", "18446744073709549568")]:
            cases.append((type_, f"%x:f64 = const {value}\n%result:{type_} = cast %x", expected))
        for type_ in ("f32", "f64"):
            cases.append((type_, f"%x:u64 = const 18446744073709551615\n%result:{type_} = cast %x", "18446744073709551616"))
        self.assertEqual(run_source(scalar_suite(cases)).exit_code, 0)

    def test_mixed_abi_register_and_stack_arguments(self):
        params = [(f"i{i}", "i64") if i % 2 == 0 else (f"f{i}", "f64") for i in range(20)]
        signature = ", ".join(f"%{name}:{type_}" for name, type_ in params)
        cparams = ", ".join(("int64_t" if type_ == "i64" else "double") + " " + name for name, type_ in params)
        body = []
        previous = "zero"
        for name, type_ in params:
            value = name
            if type_ == "f64":
                value = "c" + name
                body.append(f"%{value}:i64 = cast %{name}")
            result = "sum" + name
            body.append(f"%{result}:i64 = add %{previous}, %{value}")
            previous = result
        path = self.root / "abi.lm0"
        path.write_text(f"module abi version 1\nexport c fn @sum_all({signature}) -> i64 {{\n^entry:\n%zero:i64 = const 0\n" + "\n".join(body) + f"\nreturn %{previous}\n}}\nextern c fn @foreign({signature}) -> i64\nexport c fn @invoke() -> i64 {{\n^entry:\n" + "\n".join(f"%{name}:{type_} = const {i+1}" for i, (name, type_) in enumerate(params)) + "\n%r:i64 = call @foreign(" + ", ".join("%" + name for name, _ in params) + ")\nreturn %r\n}\n")
        helper = self.root / "host.c"
        helper.write_text("#include <stdint.h>\n" + f"extern int64_t sum_all({cparams});\nextern int64_t invoke(void);\nint64_t foreign({cparams}) {{ return " + " + ".join("(int64_t)" + name for name, _ in params) + "; }\nint main(void) { return sum_all(" + ", ".join(str(i+1) for i in range(20)) + ") != 210 || invoke() != 210; }\n")
        obj = self.root / "abi.o"
        code, result = cli("build", path, "--kind", "object", "-o", obj)
        self.assertEqual(code, 0, result)
        executable = self.root / "host"
        subprocess.run(["gcc", str(helper), str(obj), "-o", str(executable)], check=True, capture_output=True)
        self.assertEqual(subprocess.run([str(executable)]).returncode, 0)

    def test_assembler_sensitive_export_name_and_source_path(self):
        directory = self.root / "source path"
        directory.mkdir()
        path = directory / "quoted source.lm0"
        path.write_text("""module symbols version 1
export c fn @rax(%value:i32) -> i32 {
^entry:
return %value
}
fn @main() -> i32 {
^entry:
%answer:i32 = const 42
%result:i32 = call @rax(%answer)
return %result
}
""")
        code, result = cli("run", path)
        self.assertEqual(code, 0, result)
        self.assertEqual(result["exit_code"], 42)

    def test_snake_engine_build_without_host(self):
        code, result = cli("build", ROOT / "examples/snake/engine.lm0", "--kind", "shared", "-o", self.root / "snake.so")
        self.assertEqual(code, 0, result)
        symbols = subprocess.check_output(["nm", "-D", str(self.root / "snake.so")], text=True)
        self.assertIn(" snake_create", symbols)
        self.assertNotIn("lm0_fn_", symbols)
