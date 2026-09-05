from dataclasses import dataclass


@dataclass(frozen=True)
class Operation:
    syntax: str
    result: bool
    rule: str
    description: str
    terminator: bool = False


OPS: dict[str, Operation] = {}


def register(names: str, syntax: str, result: bool, rule: str, description: str,
             terminator: bool = False) -> None:
    for name in names.split():
        OPS[name] = Operation(syntax, result, rule, description, terminator)


register("const", "literal", True, "constant", "Typed scalar literal")
register("null", "none", True, "null", "Null pointer")
register("add sub mul div", "binary", True, "numeric", "Same-type numeric arithmetic")
register("rem and or xor shl shr", "binary", True, "integer", "Same-type integer operation")
register("eq ne", "binary", True, "equality", "Same-type scalar equality, producing bool")
register("lt le gt ge", "binary", True, "ordered", "Same-type numeric comparison, producing bool")
register("neg", "unary", True, "negate", "Numeric negation")
register("not", "unary", True, "complement", "Integer bit complement or Boolean negation")
register("cast", "unary", True, "cast", "Explicit numeric conversion or pointer reinterpretation")
register("ptrtoint", "unary", True, "ptrtoint", "Convert pointer to u64")
register("inttoptr", "unary", True, "inttoptr", "Convert u64 to pointer")
register("call", "call", True, "call", "Direct function call; omit destination for void")
register("stack", "type_count", True, "stack", "Fixed-size allocation in the function entry block")
register("alloc", "type_reg", True, "alloc", "Allocate count elements on the heap; count is u64")
register("free", "unary", False, "free", "Release a heap base pointer; null is allowed")
register("offset", "binary", True, "offset", "Offset typed pointer by i64 elements")
register("field", "field", True, "field", "Address of a named struct field")
register("load", "unary", True, "load", "Load a scalar from memory")
register("store", "binary", False, "store", "Store scalar to pointer")
register("copy", "ternary", False, "copy", "Copy u64 bytes between disjoint memory ranges")
register("sizeof alignof", "type", True, "size", "Target ABI size or alignment as u64")
register("address", "symbol", True, "address", "Address of static byte data as ptr<u8>")
register("jump", "target", False, "jump", "Unconditional block transfer", True)
register("branch", "branch", False, "branch", "Boolean conditional block transfer", True)
register("return", "optional_reg", False, "return", "Return scalar or void", True)
register("trap", "none", False, "trap", "Terminate with E_TRAP", True)
