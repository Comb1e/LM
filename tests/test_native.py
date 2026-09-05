from pathlib import Path
import tempfile
import unittest

from lm0.parser import parse
from lm0.tooling import build, build_c, execute, read_module
from tests.support import ROOT, run_source, scalar_suite


class NativeTests(unittest.TestCase):
    def assert_suite(self, cases, sanitize=True):
        source = scalar_suite(cases)
        for optimization in ("0", "2"):
            with self.subTest(optimization=optimization):
                result = run_source(source, optimization, sanitize)
                self.assertEqual(result.exit_code, 0, result.stderr)
                self.assertEqual(result.stderr, "")

    def test_integer_widths_wrapping_and_promotions(self):
        cases = []
        for sign in "iu":
            for width in (8, 16, 32, 64):
                type_ = f"{sign}{width}"
                maximum = (1 << (width - (sign == "i"))) - 1
                minimum = -(1 << (width - 1)) if sign == "i" else 0
                for op, a, b, expected in [
                    ("add", maximum, 1, minimum),
                    ("sub", minimum, 1, maximum),
                    ("mul", maximum, maximum, 1),
                ]:
                    cases.append((type_, f"%a:{type_} = const {a}\n%b:{type_} = const {b}\n%result:{type_} = {op} %a, %b", str(expected)))
                cases.append((type_, f"%a:{type_} = const 0\n%result:{type_} = not %a", "-1" if sign == "i" else str(maximum)))
        self.assert_suite(cases)

    def test_shifts_division_and_integer_casts(self):
        cases = []
        for width in (8, 16, 32, 64):
            for op, a, b, expected in [("shr", -8, 2, -2), ("shl", -1, 1, -2), ("div", -7, 3, -2), ("rem", -7, 3, -1)]:
                cases.append((f"i{width}", f"%a:i{width} = const {a}\n%b:i{width} = const {b}\n%result:i{width} = {op} %a, %b", str(expected)))
        cases += [
            ("i8", "%a:u64 = const 255\n%result:i8 = cast %a", "-1"),
            ("u64", "%a:i8 = const -1\n%result:u64 = cast %a", "18446744073709551615"),
            ("i64", "%a:i8 = const -128\n%result:i64 = cast %a", "-128"),
            ("i64", "%a:i64 = const -9223372036854775808\n%result:i64 = neg %a", "-9223372036854775808"),
            ("u16", "%a:u16 = const 60000\n%b:u16 = const 60000\n%result:u16 = mul %a, %b", str(60000 * 60000 % 65536)),
            ("bool", "%a:bool = const false\n%result:bool = not %a", "true"),
        ]
        self.assert_suite(cases)

    def test_floating_point(self):
        self.assert_suite([
            ("f64", "%a:f64 = const 1.5\n%b:f64 = const 2.5\n%result:f64 = add %a, %b", "4.0"),
            ("f32", "%a:f32 = const 16777216\n%b:f32 = const 1\n%result:f32 = add %a, %b", "16777216"),
            ("i64", "%a:f64 = const -3.9\n%result:i64 = cast %a", "-3"),
            ("u64", "%a:f64 = const -0.5\n%result:u64 = cast %a", "0"),
            ("f64", "%a:i64 = const -42\n%result:f64 = cast %a", "-42"),
            ("f64", "%a:f64 = const 1\n%b:f64 = const -0.0\n%result:f64 = div %a, %b", "-inf"),
            ("f64", "%a:f64 = const 0\n%result:f64 = div %a, %a", "nan"),
            ("f32", "%a:f64 = const 1e100\n%result:f32 = cast %a", "inf"),
            ("f32", "%a:f64 = const 3.4028235e38\n%result:f32 = cast %a", "3.4028234663852886e38"),
            ("f32", "%a:f64 = const -3.4028235e38\n%result:f32 = cast %a", "-3.4028234663852886e38"),
        ])

    def test_defined_traps(self):
        operations = [
            ("%a:i64 = const 1\n%b:i64 = const 0\n%v:i64 = div %a, %b", "E_DIVISION"),
            ("%a:i64 = const -9223372036854775808\n%b:i64 = const -1\n%v:i64 = rem %a, %b", "E_DIVISION"),
            ("%a:u8 = const 1\n%b:u8 = const 8\n%v:u8 = shl %a, %b", "E_SHIFT"),
            ("%a:i64 = const 1\n%b:i64 = const -1\n%v:i64 = shr %a, %b", "E_SHIFT"),
            ("%a:f64 = const nan\n%v:i32 = cast %a", "E_CAST"),
            ("%a:f64 = const 18446744073709551616\n%v:u64 = cast %a", "E_CAST"),
            ("%n:u64 = const 18446744073709551615\n%p:ptr<i64> = alloc i64, %n", "E_ALLOC"),
            ("%p:ptr<i64> = null\n%i:i64 = const 9223372036854775807\n%q:ptr<i64> = offset %p, %i", "E_OFFSET"),
            ("trap", "E_TRAP"),
        ]
        for body, code in operations:
            with self.subTest(code=code, body=body):
                end = "" if body == "trap" else "\n%x:i32 = const 0\nreturn %x"
                result = run_source("module traps version 1\nfn @main() -> i32 {\n^entry:\n" + body + end + "\n}\n", "2", True)
                self.assertEqual(result.exit_code, 70, result.stderr)
                self.assertIn(code, result.stderr)
                self.assertNotIn("Sanitizer", result.stderr)

    def test_examples(self):
        examples = {"add": 42, "array_sum": 42, "binary_search": 1, "sort": 2, "linked_list": 42, "strings": 14}
        for name, expected in examples.items():
            for optimization in ("0", "2"):
                with self.subTest(example=name, optimization=optimization):
                    result = run_source((ROOT / "examples" / (name + ".lm0")).read_text(), optimization, True)
                    self.assertEqual(result.exit_code, expected, result.stderr)
                    self.assertEqual(result.stderr, "")
                    if name == "strings":
                        self.assertEqual(result.stdout, "Hello from LM0\n")

    def test_parallel_block_arguments_and_lazy_branch(self):
        source = """module swaps version 1
fn @main() -> i32 {
^entry:
    %one:i32 = const 1
    %two:i32 = const 2
    %zero:i32 = const 0
    jump ^loop(%one, %two, %zero)
^loop(%a:i32, %b:i32, %iteration:i32):
    %limit:i32 = const 1
    %again:bool = lt %iteration, %limit
    branch %again, ^loop(%b, %a, %limit), ^done(%a, %b)
^done(%first:i32, %second:i32):
    %expected:i32 = const 2
    %correct:bool = eq %first, %expected
    branch %correct, ^success(%second), ^bad()
^success(%result:i32):
    return %result
^bad:
    trap
}
"""
        for optimization in ("0", "2"):
            result = run_source(source, optimization, True)
            self.assertEqual(result.exit_code, 1, result.stderr)
            self.assertEqual(result.stderr, "")

    def test_recursion_and_forward_calls(self):
        source = """module recursion version 1
fn @main() -> i32 {
^entry:
    %n:i32 = const 5
    %result:i32 = call @factorial(%n)
    return %result
}
fn @factorial(%n:i32) -> i32 {
^entry:
    %one:i32 = const 1
    %base:bool = le %n, %one
    branch %base, ^done(), ^recurse()
^done:
    %identity:i32 = const 1
    return %identity
^recurse:
    %step:i32 = const 1
    %previous:i32 = sub %n, %step
    %smaller:i32 = call @factorial(%previous)
    %result:i32 = mul %n, %smaller
    return %result
}
"""
        result = run_source(source, "2", True)
        self.assertEqual(result.exit_code, 120, result.stderr)

    def test_unaligned_memory_arrays_copy_and_pointer_roundtrip(self):
        source = """module memory version 1
fn @main() -> i32 {
^entry:
    %bytes:ptr<u8> = stack u8, 24
    %one:i64 = const 1
    %unaligned:ptr<u8> = offset %bytes, %one
    %word:ptr<i64> = cast %unaligned
    %value:i64 = const 42
    store %word, %value
    %destination:ptr<[i32;2]> = stack [i32;2], 1
    %size:u64 = sizeof i64
    copy %destination, %word, %size
    %bits:u64 = ptrtoint %destination
    %restored:ptr<i64> = inttoptr %bits
    %copied:i64 = load %restored
    %negative:i64 = const -1
    %base:ptr<u8> = offset %unaligned, %negative
    %same:bool = eq %base, %bytes
    branch %same, ^done(%copied), ^bad()
^done(%answer:i64):
    %result:i32 = cast %answer
    return %result
^bad:
    trap
}
"""
        result = run_source(source, "2", True)
        self.assertEqual(result.exit_code, 42, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_layout_matches_c_and_exported_object(self):
        source = """module layouts version 1
struct Record {
    byte:u8
    items:[i32;3]
    pointer:ptr<Record>
}
extern c fn @check_layout(%size:u64, %alignment:u64, %field_offset:u64) -> i32
fn @main() -> i32 {
^entry:
    %p:ptr<Record> = stack Record, 1
    %q:ptr<ptr<Record>> = field %p, pointer
    %a:u64 = ptrtoint %p
    %b:u64 = ptrtoint %q
    %offset:u64 = sub %b, %a
    %size:u64 = sizeof Record
    %alignment:u64 = alignof Record
    %result:i32 = call @check_layout(%size, %alignment, %offset)
    return %result
}
"""
        helper = """#include <stdint.h>
#include <stddef.h>
struct Record { uint8_t byte; int32_t items[3]; struct Record *pointer; };
int32_t check_layout(uint64_t size, uint64_t alignment, uint64_t offset) {
    return size != sizeof(struct Record) || alignment != _Alignof(struct Record) || offset != offsetof(struct Record, pointer);
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            c_file = root / "helper.c"
            c_file.write_text(helper)
            program = root / "program"
            build(parse(source), program, links=[c_file], optimization="2", sanitize=True)
            result = execute([str(program)], 5, 100000)
            self.assertEqual(result.exit_code, 0, result.stderr)
            object_ = root / "ffi.o"
            build(read_module(ROOT / "examples/ffi.lm0"), object_, kind="object")
            build_c((ROOT / "examples/ffi_driver.c").read_text(), program, links=[object_])
            result = execute([str(program)], 5, 100000)
            self.assertEqual(result.stdout, "42\n")
            self.assertEqual(result.exit_code, 0, result.stderr)

    def test_sanitizer_detects_use_after_free(self):
        source = """module invalid_memory version 1
fn @main() -> i32 {
^entry:
    %one:u64 = const 1
    %p:ptr<i32> = alloc i32, %one
    %value:i32 = const 42
    store %p, %value
    free %p
    %result:i32 = load %p
    return %result
}
"""
        result = run_source(source, "0", True)
        self.assertNotEqual(result.exit_code, 0)
        self.assertIn("heap-use-after-free", result.stderr)
