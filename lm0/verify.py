"""Semantic validation shared by checking, compilation, inspection, and repairs."""

import math
import struct as binary

from .config import DEFAULTS
from .model import CompileError, Diagnostic, Instruction, Module, Type
from .ops import OPS


SCALARS = {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"}
VOID, BOOL, U64, I64 = map(Type, ("void", "bool", "u64", "i64"))


def literal(text: str, type_: Type):
    if type_.name == "bool":
        if text not in {"true", "false"}:
            raise ValueError("Boolean literal must be true or false")
        return text == "true"
    if type_.integer:
        value = int(text, 16 if "x" in text.lower() else 10)
        low = -(1 << (type_.bits - 1)) if type_.signed else 0
        high = (1 << (type_.bits - int(type_.signed))) - 1
        if not low <= value <= high:
            raise ValueError(f"Literal is outside {type_} range")
        return value
    if type_.floating:
        value = float(text)
        if not math.isfinite(value) and text not in {"inf", "-inf", "nan"}:
            raise ValueError("Overflowing float literal; use inf explicitly")
        if type_.name == "f32":
            value = binary.unpack("f", binary.pack("f", value))[0]
            if not math.isfinite(value) and text not in {"inf", "-inf", "nan"}:
                raise ValueError("Overflowing f32 literal; use inf explicitly")
        return value
    raise ValueError("const requires a numeric or Boolean type; use null for pointers")


class InvalidInstruction(Exception):
    pass


class Verifier:
    def __init__(self, module: Module, config: dict):
        self.module, self.config = module, config
        self.diagnostics = []
        self.functions = {f.name: f for f in module.functions}
        self.structs = {s.name: s for s in module.structs}
        self.data = {d.name: d for d in module.data}
        self.function = None
        self.block = None
        self.instruction = None
        self.env = {}
        self.blocks = {}
        self.layouts = {}

    def error(self, code, message, span=None, expected=None, actual=None, register=None):
        if len(self.diagnostics) < self.config["limits"]["diagnostics"]:
            self.diagnostics.append(Diagnostic(
                code, "verify", message,
                span or (self.instruction.span if self.instruction else None),
                self.function.name if self.function else None,
                self.block.name if self.block else None, register, expected, actual))

    def require(self, condition, code, message, **details):
        if not condition:
            self.error(code, message, **details)
            raise InvalidInstruction()

    def valid_type(self, type_: Type, span, allow_void=False) -> bool:
        if type_.name == "void":
            if not allow_void:
                self.error("E_TYPE", "void is not a value or storage type", span)
            return allow_void
        if type_.name == "ptr":
            return self.valid_type(type_.element, span, True)
        if type_.name == "array":
            valid = self.valid_type(type_.element, span)
            if type_.count <= 0 or type_.count > self.config["limits"]["aggregate_bytes"]:
                self.error("E_LAYOUT", "Array count is outside supported limits", span)
                return False
            return valid
        if type_.name not in SCALARS and type_.name not in self.structs:
            self.error("E_TYPE", "Unknown type", span, actual=str(type_))
            return False
        return True

    def layout(self, type_: Type, active=()) -> tuple[int, int]:
        if type_ in self.layouts:
            return self.layouts[type_]
        if type_.name in active:
            raise ValueError("Struct has a recursive by-value layout")
        if type_.name == "bool":
            size, alignment = 1, 1
        elif type_.numeric:
            size = alignment = type_.bits // 8
        elif type_.name == "ptr":
            size, alignment = 8, 8
        elif type_.name == "array":
            item_size, alignment = self.layout(type_.element, active)
            size = item_size * type_.count
        elif type_.name in self.structs:
            size, alignment = 0, 1
            for field in self.structs[type_.name].fields:
                field_size, field_align = self.layout(field.type, (*active, type_.name))
                size = (size + field_align - 1) // field_align * field_align + field_size
                alignment = max(alignment, field_align)
            size = (size + alignment - 1) // alignment * alignment
        else:
            raise ValueError(f"Type {type_} has no storage layout")
        if size <= 0 or size > self.config["limits"]["aggregate_bytes"]:
            raise ValueError("Storage size is outside configured limits")
        self.layouts[type_] = size, alignment
        return size, alignment

    def storage(self, type_: Type):
        self.require(self.valid_type(type_, self.instruction.span), "E_TYPE", "Invalid storage type")
        try:
            return self.layout(type_)
        except ValueError as error:
            self.require(False, "E_LAYOUT", str(error))

    def reg(self, name: str) -> Type:
        self.require(name in self.env, "E_REGISTER", "Register is unavailable in this block",
                     register=name)
        return self.env[name]

    def same(self, actual: Type, expected: Type):
        self.require(actual == expected, "E_TYPE", "Type mismatch",
                     expected=str(expected), actual=str(actual))

    def target(self, target):
        self.require(target.name in self.blocks, "E_BLOCK", "Unknown target block", actual=target.name)
        self.require(target.name != self.function.blocks[0].name, "E_ENTRY", "Cannot branch to entry block")
        block = self.blocks[target.name]
        self.require(len(target.args) == len(block.params), "E_ARITY", "Block argument count mismatch",
                     expected=len(block.params), actual=len(target.args))
        for arg, param in zip(target.args, block.params):
            self.same(self.reg(arg), param.type)

    def check_instruction(self, ins: Instruction):
        self.instruction = ins
        op, args = OPS[ins.op], ins.args
        rule = op.rule
        dest = ins.dest.type if ins.dest else VOID
        if ins.op != "call":
            self.require(bool(ins.dest) == op.result, "E_RESULT", "Incorrect destination presence",
                         expected=op.result, actual=bool(ins.dest))
        if ins.dest:
            self.require(dest.scalar, "E_TYPE", "Registers must have scalar or pointer types")
        if rule == "constant":
            try:
                literal(args[0], dest)
            except (ValueError, OverflowError) as error:
                self.require(False, "E_LITERAL", str(error))
        elif rule == "null":
            self.require(dest.name == "ptr", "E_TYPE", "null requires a pointer destination")
        elif rule in {"numeric", "integer", "equality", "ordered"}:
            a, b = map(self.reg, args)
            self.same(b, a)
            allowed = a.integer if rule == "integer" else a.scalar if rule == "equality" else a.numeric
            self.require(allowed, "E_TYPE", "Operand type is not supported by this instruction",
                         actual=str(a))
            self.same(dest, BOOL if rule in {"equality", "ordered"} else a)
        elif rule in {"negate", "complement"}:
            a = self.reg(args[0])
            self.require(a.numeric if rule == "negate" else a.integer or a == BOOL,
                         "E_TYPE", "Unsupported unary operand type", actual=str(a))
            self.same(dest, a)
        elif rule == "cast":
            a = self.reg(args[0])
            self.require((a.numeric and dest.numeric) or (a.name == dest.name == "ptr"),
                         "E_CAST", "cast supports numeric-to-numeric or pointer-to-pointer conversions")
        elif rule == "ptrtoint":
            self.require(self.reg(args[0]).name == "ptr", "E_TYPE", "Expected pointer")
            self.same(dest, U64)
        elif rule == "inttoptr":
            self.same(self.reg(args[0]), U64)
            self.require(dest.name == "ptr", "E_TYPE", "Expected pointer result")
        elif rule == "call":
            self.require(args[0] in self.functions, "E_FUNCTION", "Unknown function", actual=args[0])
            callee = self.functions[args[0]]
            self.require(len(args[1]) == len(callee.params), "E_ARITY", "Function argument count mismatch",
                         expected=len(callee.params), actual=len(args[1]))
            for arg, param in zip(args[1], callee.params):
                self.same(self.reg(arg), param.type)
            self.same(dest, callee.returns)
        elif rule in {"stack", "alloc"}:
            size, _ = self.storage(args[0])
            self.same(dest, Type("ptr", args[0]))
            if rule == "stack":
                self.require(self.block is self.function.blocks[0], "E_STACK", "stack is only legal in entry")
                self.require(0 < args[1] <= self.config["limits"]["aggregate_bytes"] // size,
                             "E_LAYOUT", "Stack allocation size is outside configured limits")
            else:
                self.same(self.reg(args[1]), U64)
        elif rule == "free":
            self.require(self.reg(args[0]).name == "ptr", "E_TYPE", "free requires a pointer")
        elif rule == "offset":
            a = self.reg(args[0])
            self.require(a.name == "ptr", "E_TYPE", "offset requires a pointer")
            self.storage(a.element)
            self.same(self.reg(args[1]), I64)
            self.same(dest, a)
        elif rule == "field":
            a = self.reg(args[0])
            self.require(a.name == "ptr" and a.element.name in self.structs,
                         "E_TYPE", "field requires a struct pointer")
            fields = {f.name: f.type for f in self.structs[a.element.name].fields}
            self.require(args[1] in fields, "E_FIELD", "Unknown struct field", actual=args[1])
            self.same(dest, Type("ptr", fields[args[1]]))
        elif rule == "load":
            self.same(self.reg(args[0]), Type("ptr", dest))
        elif rule == "store":
            self.same(self.reg(args[0]), Type("ptr", self.reg(args[1])))
        elif rule == "copy":
            self.require(all(self.reg(arg).name == "ptr" for arg in args[:2]),
                         "E_TYPE", f"{ins.op} requires two pointers")
            self.same(self.reg(args[2]), U64)
        elif rule == "size":
            self.storage(args[0])
            self.same(dest, U64)
        elif rule == "address":
            self.require(args[0] in self.data, "E_DATA", "Unknown static data", actual=args[0])
            self.same(dest, Type("ptr", Type("u8")))
        elif rule == "jump":
            self.target(args[0])
        elif rule == "branch":
            self.same(self.reg(args[0]), BOOL)
            self.target(args[1])
            self.target(args[2])
        elif rule == "return":
            self.same(self.reg(args[0]) if args else VOID, self.function.returns)

    def unique(self, name, seen, span, kind):
        if name in seen:
            self.error("E_DUPLICATE", f"Duplicate {kind}: {name}", span)
        seen.add(name)

    def run(self):
        if self.module.version != 1:
            self.error("E_VERSION", "Unsupported language version", expected=1, actual=self.module.version)
        seen = set()
        for item in [*self.module.functions, *self.module.data]:
            self.unique(item.name, seen, item.span, "symbol")
        seen = set(SCALARS) | {"void", "ptr", "array"}
        for struct in self.module.structs:
            self.unique(struct.name, seen, struct.span, "type")
            field_names = set()
            for field in struct.fields:
                self.unique(field.name, field_names, field.span, "field")
                self.valid_type(field.type, field.span)
            try:
                self.layout(Type(struct.name))
            except (ValueError, RecursionError) as error:
                self.error("E_LAYOUT", str(error), struct.span)
        for function in self.module.functions:
            self.function, self.block, self.instruction = function, None, None
            self.valid_type(function.returns, function.span, True)
            if function.returns != VOID and not function.returns.scalar:
                self.error("E_ABI", "Function return must be scalar, pointer, or void", function.span)
            if (function.external or function.exported) and (function.name.startswith("lm0_") or function.name == "main"):
                self.error("E_ABI", "C symbols main and lm0_* are reserved", function.span)
            names = set()
            for param in function.params:
                self.unique(param.name, names, param.span, "register")
                self.valid_type(param.type, param.span)
                if not param.type.scalar:
                    self.error("E_ABI", "Function parameters must be scalar or pointer", param.span)
            if function.external:
                continue
            if not function.blocks:
                self.error("E_BLOCK", "Function must have an entry block", function.span)
                continue
            if function.blocks[0].params:
                self.error("E_ENTRY", "Entry block cannot have block parameters", function.blocks[0].span)
            self.blocks = {b.name: b for b in function.blocks}
            block_names = set()
            for block in function.blocks:
                self.block, self.instruction = block, None
                self.unique(block.name, block_names, block.span, "block")
                self.env = {p.name: p.type for p in function.params}
                for param in block.params:
                    self.unique(param.name, names, param.span, "register")
                    self.valid_type(param.type, param.span)
                    if not param.type.scalar:
                        self.error("E_TYPE", "Block parameters must be scalar or pointer", param.span)
                    self.env[param.name] = param.type
                if not block.instructions or not OPS[block.instructions[-1].op].terminator:
                    self.error("E_TERMINATOR", "Block must end with a terminator", block.span)
                for index, ins in enumerate(block.instructions):
                    if OPS[ins.op].terminator and index != len(block.instructions) - 1:
                        self.error("E_TERMINATOR", "Instruction follows a terminator", ins.span)
                    if ins.dest:
                        self.unique(ins.dest.name, names, ins.dest.span, "register")
                        self.valid_type(ins.dest.type, ins.dest.span)
                    try:
                        self.check_instruction(ins)
                    except InvalidInstruction:
                        pass
                    if ins.dest:
                        self.env[ins.dest.name] = ins.dest.type
        if self.diagnostics:
            raise CompileError(self.diagnostics)
        return self.module


def verify(module: Module, config: dict | None = None) -> Module:
    return Verifier(module, config or DEFAULTS).run()
