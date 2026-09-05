"""C11 lowering. GCC owns optimization, ABI lowering, and register allocation."""

import json
import math
from pathlib import Path

from .model import Diagnostic, Module, Type
from .verify import literal, verify


CTYPES = {"bool": "bool", "void": "void", "f32": "float", "f64": "double"}
CTYPES.update({f"{sign}{n}": f"{'u' if sign == 'u' else ''}int{n}_t"
               for sign in "iu" for n in (8, 16, 32, 64)})


def c_string(value: str) -> str:
    # Fixed-width octal escapes cannot absorb a following digit in generated C.
    return '"' + "".join(chr(b) if 32 <= b < 127 and b not in (34, 92)
                         else f"\\{b:03o}" for b in value.encode("utf-8")) + '"'


def reg(name: str) -> str:
    return "lm0_r_" + name


class Emitter:
    def __init__(self, module: Module):
        self.module = module
        self.structs = {s.name: s for s in module.structs}
        self.functions = {f.name: f for f in module.functions}
        self.type_names = {}
        self.definitions = []
        self.defined = set()
        self.function = None
        self.block = None
        self.instruction = None
        self.types = {}
        self.edge_number = 0

    def ctype(self, type_: Type) -> str:
        if type_.name == "ptr":
            return "void *"
        if type_.name in CTYPES:
            return CTYPES[type_.name]
        self.define_type(type_)
        return self.type_names[type_]

    def define_type(self, type_: Type):
        if type_ in self.defined:
            return
        self.defined.add(type_)
        if type_.name == "array":
            name = f"lm0_array_{len(self.type_names)}"
            self.type_names[type_] = name
            element = self.ctype(type_.element)
            self.definitions.append(f"typedef {element} {name}[{type_.count}];")
        elif type_.name in self.structs:
            name = "lm0_s_" + type_.name
            self.type_names[type_] = name
            fields = [f"    {self.ctype(f.type)} lm0_m_{f.name};" for f in self.structs[type_.name].fields]
            self.definitions.append(f"struct {name} {{\n" + "\n".join(fields) + "\n};")

    def error(self, code: str, message: str) -> str:
        diagnostic = Diagnostic(code, "runtime", message, self.instruction.span,
                                self.function.name, self.block.name,
                                self.instruction.dest.name if self.instruction.dest else None)
        return c_string(json.dumps({"ok": False, "diagnostics": [diagnostic.json()]}, separators=(",", ":")))

    def trap(self, code, message):
        return "lm0_trap(" + self.error(code, message) + ");"

    def wrap(self, expression: str, type_: Type) -> str:
        converted = f"(uint{type_.bits}_t)({expression})"
        return f"lm0_i{type_.bits}({converted})" if type_.signed else converted

    def constant(self, text: str, type_: Type) -> str:
        value = literal(text, type_)
        if type_.integer:
            bits = value & ((1 << type_.bits) - 1)
            return self.wrap(f"UINT64_C({bits})", type_)
        if type_.name == "bool":
            return "true" if value else "false"
        if math.isnan(value):
            return "NAN"
        if math.isinf(value):
            return "INFINITY" if value > 0 else "(-INFINITY)"
        return value.hex() + ("f" if type_.name == "f32" else "")

    def signature(self, function, names=False) -> str:
        params = [self.ctype(p.type) + (" " + reg(p.name) if names else "") for p in function.params]
        linkage = "" if function.external or function.exported else "static "
        return f"{linkage}{self.ctype(function.returns)} lm0_fn_{function.name}({', '.join(params) or 'void'})"

    def edge(self, target) -> list[str]:
        block = next(b for b in self.function.blocks if b.name == target.name)
        self.edge_number += 1
        prefix = f"lm0_edge_{self.edge_number}_"
        lines = [f"{self.ctype(p.type)} {prefix}{i} = {reg(a)};"
                 for i, (p, a) in enumerate(zip(block.params, target.args))]
        lines += [f"{reg(p.name)} = {prefix}{i};" for i, p in enumerate(block.params)]
        return ["{", *("    " + line for line in lines), f"    goto lm0_b_{target.name};", "}"]

    def instruction_lines(self, ins) -> list[str]:
        self.instruction = ins
        op, args = ins.op, ins.args
        dest = ins.dest.type if ins.dest else Type("void")
        output = []
        expression = None
        a = reg(args[0]) if args and isinstance(args[0], str) else None
        b = reg(args[1]) if len(args) > 1 and isinstance(args[1], str) else None
        if op == "const":
            expression = self.constant(args[0], dest)
        elif op == "null":
            expression = "NULL"
        elif op in {"add", "sub", "mul", "and", "or", "xor"}:
            operator = {"add": "+", "sub": "-", "mul": "*", "and": "&", "or": "|", "xor": "^"}[op]
            expression = (self.wrap(f"(uint64_t){a} {operator} (uint64_t){b}", dest)
                          if dest.integer else f"{a} {operator} {b}")
        elif op in {"div", "rem"}:
            if dest.integer:
                condition = f"{b} == 0"
                if dest.signed:
                    condition += f" || ({a} == INT{dest.bits}_MIN && {b} == -1)"
                output.append(f"if ({condition}) " + self.trap("E_DIVISION", "Invalid integer division"))
            expression = f"{a} {'/' if op == 'div' else '%'} {b}"
        elif op in {"shl", "shr"}:
            output.append(f"if ((uint64_t){b} >= {dest.bits}) " + self.trap("E_SHIFT", "Shift count is outside type width"))
            if op == "shr" and dest.signed:
                expression = self.wrap(f"lm0_sar((uint64_t){a}, {dest.bits}, (unsigned){b})", dest)
            else:
                expression = self.wrap(f"(uint64_t){a} {'<<' if op == 'shl' else '>>'} {b}", dest)
        elif op in {"eq", "ne", "lt", "le", "gt", "ge"}:
            operator = {"eq": "==", "ne": "!=", "lt": "<", "le": "<=", "gt": ">", "ge": ">="}[op]
            expression = f"{a} {operator} {b}"
        elif op == "neg":
            expression = self.wrap(f"UINT64_C(0) - (uint64_t){a}", dest) if dest.integer else f"-{a}"
        elif op == "not":
            expression = self.wrap(f"~(uint64_t){a}", dest) if dest.integer else f"!{a}"
        elif op == "cast":
            source = self.types[args[0]]
            if dest.integer and source.integer:
                expression = self.wrap(f"(uint64_t){a}", dest)
            elif dest.integer and source.floating:
                low = f"-0x1p{dest.bits - 1}L" if dest.signed else "0.0L"
                high = f"0x1p{dest.bits - int(dest.signed)}L"
                output.append(f"long double lm0_cast_{ins.dest.name} = truncl((long double){a});")
                temp = "lm0_cast_" + ins.dest.name
                output.append(f"if (!isfinite({a}) || {temp} < {low} || {temp} >= {high}) " +
                              self.trap("E_CAST", "Float-to-integer conversion is outside range"))
                expression = f"({self.ctype(dest)}){temp}"
            elif dest.name == "f32" and source.name == "f64":
                expression = f"lm0_f32({a})"
            else:
                expression = f"({self.ctype(dest)}){a}"
        elif op == "ptrtoint":
            expression = f"(uint64_t)(uintptr_t){a}"
        elif op == "inttoptr":
            expression = f"(void *)(uintptr_t){a}"
        elif op == "call":
            expression = f"lm0_fn_{args[0]}({', '.join(reg(v) for v in args[1])})"
        elif op == "stack":
            expression = "lm0_stack_" + ins.dest.name
        elif op == "alloc":
            expression = (f"lm0_alloc({reg(args[1])}, sizeof({self.ctype(args[0])}), " +
                          self.error("E_ALLOC", "Allocation failed or size overflowed") + ")")
        elif op == "free":
            output.append(f"free({a});")
        elif op == "offset":
            expression = (f"lm0_offset({a}, {b}, sizeof({self.ctype(dest.element)}), " +
                          self.error("E_OFFSET", "Pointer offset exceeds target range") + ")")
        elif op == "field":
            base = self.types[args[0]].element
            expression = f"(unsigned char *){a} + offsetof({self.ctype(base)}, lm0_m_{args[1]})"
        elif op == "load":
            output.append(f"memcpy(&{reg(ins.dest.name)}, {a}, sizeof({reg(ins.dest.name)}));")
        elif op == "store":
            output.append(f"memcpy({a}, &{b}, sizeof({b}));")
        elif op == "copy":
            output.append(f"if ({reg(args[2])}) memcpy({a}, {b}, (size_t){reg(args[2])});")
        elif op in {"sizeof", "alignof"}:
            expression = f"{'sizeof' if op == 'sizeof' else '_Alignof'}({self.ctype(args[0])})"
        elif op == "address":
            expression = f"lm0_data_{args[0]}"
        elif op == "jump":
            output += self.edge(args[0])
        elif op == "branch":
            output += [f"if ({a}) {{", *("    " + line for line in self.edge(args[1])), "} else {",
                       *("    " + line for line in self.edge(args[2])), "}"]
        elif op == "return":
            output.append("return" + (" " + a if args else "") + ";")
        elif op == "trap":
            output.append(self.trap("E_TRAP", "Explicit trap"))
        else:
            raise AssertionError(f"Missing code generation for {op}")
        if expression is not None:
            output.append((reg(ins.dest.name) + " = " if ins.dest else "") + expression + ";")
        return output

    def emit_function(self, function):
        self.function = function
        self.types = {p.name: p.type for p in function.params}
        locals_ = []
        stack = []
        for block in function.blocks:
            for param in [*block.params, *(i.dest for i in block.instructions if i.dest)]:
                self.types[param.name] = param.type
                locals_.append(f"    {self.ctype(param.type)} {reg(param.name)};")
            for ins in block.instructions:
                if ins.op == "stack":
                    stack.append(f"    {self.ctype(ins.args[0])} lm0_stack_{ins.dest.name}[{ins.args[1]}];")
        lines = [self.signature(function, True) + " {", *locals_, *stack]
        for block in function.blocks:
            self.block = block
            lines.append(f"lm0_b_{block.name}: {{")
            for ins in block.instructions:
                lines.append(f"#line {ins.span.line} {c_string(self.module.filename)}")
                lines.extend("    " + line for line in self.instruction_lines(ins))
            lines.append("}")
        lines.append("}")
        return "\n".join(lines)

    def emit(self, executable=False):
        for struct in self.module.structs:
            self.define_type(Type(struct.name))
        forwards = [f"typedef struct lm0_s_{s.name} lm0_s_{s.name};" for s in self.module.structs]
        prototypes = []
        for function in self.module.functions:
            signature = self.signature(function)
            if function.external or function.exported:
                signature += f" __asm__({c_string(function.name)})"
            prototypes.append(signature + ";")
        data = [f"static unsigned char lm0_data_{d.name}[{max(1, len(d.value))}] = {{" +
                ", ".join(str(b) for b in d.value or b"\0") + "};" for d in self.module.data]
        bodies = [self.emit_function(f) for f in self.module.functions if not f.external]
        if executable:
            bodies.append("int main(void) { return (int)lm0_fn_main(); }")
        return "\n\n".join(["/* Generated by LM0 0.1. Do not edit. */",
                            Path(__file__).with_name("runtime.h").read_text(),
                            *forwards, *self.definitions, *data, *prototypes, *bodies]) + "\n"


def emit_c(module: Module, executable: bool = False, config: dict | None = None) -> str:
    verify(module, config)
    if executable:
        function = next((f for f in module.functions if f.name == "main" and not f.external), None)
        if not function or function.params or function.returns != Type("i32"):
            from .model import CompileError
            raise CompileError([Diagnostic("E_ENTRY", "verify", "Executable requires fn @main() -> i32")])
    return Emitter(module).emit(executable)
