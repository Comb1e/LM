import unittest

from lm0.model import CompileError
from lm0.parser import parse
from lm0.verify import verify
from lm0.benchmark import SUM_SOURCE, repairs


class FrontendTests(unittest.TestCase):
    def rejected(self, source, code):
        with self.assertRaises(CompileError) as caught:
            verify(parse(source, "input.lm0"))
        self.assertIn(code, [d.code for d in caught.exception.diagnostics])
        return caught.exception.diagnostics

    def function(self, body, returns="i32"):
        return f"module test version 1\nfn @main() -> {returns} {{\n^entry:\n{body}\n}}\n"

    def test_comments_and_crlf(self):
        verify(parse(self.function("    %answer:i32 = const 42 # comment\n    return %answer").replace("\n", "\r\n")))

    def test_unknown_opcode_location(self):
        diagnostics = self.rejected(self.function("    %a:i32 = multiply %a, %a\n    return %a"), "E_OPCODE")
        self.assertEqual(diagnostics[0].span.line, 4)
        self.assertEqual(diagnostics[0].span.column, 14)

    def test_repair_fixtures_cover_expected_errors(self):
        codes = ["E_REGISTER", "E_TYPE", "E_TERMINATOR", "E_DUPLICATE", "E_ARITY", "E_LITERAL", "E_OPCODE", "E_TYPE"]
        for fixture, code in zip(list(repairs().values())[:8], codes):
            with self.subTest(code=code):
                self.rejected(fixture["source"], code)
        verify(parse(SUM_SOURCE))

    def test_bad_register_diagnostic_has_context(self):
        diagnostics = self.rejected(self.function("    return %missing"), "E_REGISTER")
        diagnostic = next(d for d in diagnostics if d.code == "E_REGISTER").json()
        self.assertEqual((diagnostic["function"], diagnostic["block"], diagnostic["register"]), ("main", "entry", "missing"))
        self.assertEqual(diagnostic["span"]["file"], "input.lm0")

    def test_register_scope_and_shadowing(self):
        source = self.function("    %a:i32 = const 1\n    jump ^next()\n^next:\n    return %a")
        self.rejected(source, "E_REGISTER")
        self.rejected(self.function("    %a:i32 = const 1\n    %a:i32 = const 2\n    return %a"), "E_DUPLICATE")
        self.rejected(self.function("    %a:i32 = add %a, %a\n    return %a"), "E_REGISTER")

    def test_terminators(self):
        self.rejected(self.function("    %a:i32 = const 1"), "E_TERMINATOR")
        self.rejected(self.function("    trap\n    %a:i32 = const 1\n    return %a"), "E_TERMINATOR")
        self.rejected(self.function("    jump ^entry()"), "E_ENTRY")
        self.rejected(self.function("    jump ^missing()"), "E_BLOCK")

    def test_no_implicit_numeric_conversions(self):
        diagnostics = self.rejected(self.function("    %a:i32 = const 1\n    %b:u32 = const 2\n    %c:i32 = add %a, %b\n    return %c"), "E_TYPE")
        self.assertTrue(any(d.expected == "i32" and d.actual == "u32" for d in diagnostics))
        self.rejected(self.function("    %a:bool = const true\n    %c:i32 = cast %a\n    return %c"), "E_CAST")

    def test_literals(self):
        for type_, text in [("i8", "128"), ("u8", "-1"), ("bool", "1"), ("f32", "1e100"), ("f64", "1e1000")]:
            with self.subTest(type=type_, text=text):
                self.rejected(self.function(f"    %a:{type_} = const {text}\n    return %a", type_), "E_LITERAL")

    def test_structs_and_storage(self):
        self.rejected("module m version 1\nstruct A {\n    self:A\n}\n", "E_LAYOUT")
        self.rejected("module m version 1\nstruct A {\n    b:B\n}\nstruct B {\n    a:[A;2]\n}\n", "E_LAYOUT")
        verify(parse("module m version 1\nstruct A {\n    next:ptr<A>\n}\n"))
        self.rejected(self.function("    %p:ptr<i32> = stack i32, 0\n    trap"), "E_LAYOUT")
        self.rejected(self.function("    %p:ptr<[i32;0]> = stack [i32;0], 1\n    trap"), "E_LAYOUT")
        self.rejected(self.function("    %p:ptr<missing> = null\n    trap"), "E_TYPE")
        self.rejected(self.function("    %p:ptr<void> = null\n    %i:i64 = const 1\n    %q:ptr<void> = offset %p, %i\n    trap"), "E_TYPE")

    def test_stack_in_non_entry_block(self):
        self.rejected(self.function("    jump ^next()\n^next:\n    %p:ptr<i32> = stack i32, 1\n    trap"), "E_STACK")

    def test_memory_types(self):
        self.rejected(self.function("    %p:ptr<i32> = stack i32, 1\n    %a:u32 = const 1\n    store %p, %a\n    trap"), "E_TYPE")
        self.rejected(self.function("    %p:i32 = const 1\n    free %p\n    trap"), "E_TYPE")
        self.rejected(self.function("    %x:u64 = sizeof void\n    trap"), "E_TYPE")
        for operation in ("copy", "move"):
            self.rejected(self.function(f"%p:ptr<u8> = null\n%n:i64 = const 0\n{operation} %p, %p, %n\ntrap"), "E_TYPE")
            self.rejected(self.function(f"%p:u64 = const 0\n{operation} %p, %p, %p\ntrap"), "E_TYPE")
            self.rejected(self.function(f"%p:ptr<u8> = null\n%n:u64 = const 0\n%x:u64 = {operation} %p, %p, %n\ntrap"), "E_RESULT")

    def test_invalid_calls_and_abi(self):
        self.rejected(self.function("    %a:i32 = call @missing()\n    return %a"), "E_FUNCTION")
        self.rejected("module m version 1\nextern c fn @f(%v:[i32;2]) -> void\n", "E_ABI")
        self.rejected("module m version 1\nextern c fn @main() -> i32\n", "E_ABI")
        self.rejected(self.function("    return"), "E_TYPE")

    def test_malformed_input(self):
        for source in ["", "module m version 1\nfn @f() -> i32 {", "module m version 1\n$", "module m version 1\ndata @x = \"bad\\q\"\n"]:
            with self.subTest(source=source):
                with self.assertRaises(CompileError):
                    parse(source)
        self.rejected("module m version 2\n", "E_VERSION")
