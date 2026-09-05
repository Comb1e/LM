from dataclasses import dataclass, field, asdict
from typing import Any


@dataclass(frozen=True)
class Span:
    file: str
    line: int
    column: int
    end_line: int
    end_column: int
    start: int = field(default=0, repr=False)
    end: int = field(default=0, repr=False)

    def json(self) -> dict:
        return {k: v for k, v in asdict(self).items() if k not in {"start", "end"}}


@dataclass
class Diagnostic:
    code: str
    phase: str
    message: str
    span: Span | None = None
    function: str | None = None
    block: str | None = None
    register: str | None = None
    expected: Any = None
    actual: Any = None

    def json(self) -> dict:
        data = {k: v for k, v in asdict(self).items() if v is not None}
        if self.span:
            data["span"] = self.span.json()
        return data


class CompileError(Exception):
    def __init__(self, diagnostics: list[Diagnostic]):
        self.diagnostics = diagnostics
        super().__init__("; ".join(d.message for d in diagnostics))


@dataclass(frozen=True)
class Type:
    name: str
    element: "Type | None" = None
    count: int = 0

    def __str__(self) -> str:
        if self.name == "ptr":
            return f"ptr<{self.element}>"
        if self.name == "array":
            return f"[{self.element};{self.count}]"
        return self.name

    @property
    def integer(self) -> bool:
        return self.name in {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"}

    @property
    def floating(self) -> bool:
        return self.name in {"f32", "f64"}

    @property
    def numeric(self) -> bool:
        return self.integer or self.floating

    @property
    def signed(self) -> bool:
        return self.integer and self.name.startswith("i")

    @property
    def bits(self) -> int:
        return int(self.name[1:])

    @property
    def scalar(self) -> bool:
        return self.numeric or self.name in {"bool", "ptr"}


@dataclass
class Param:
    name: str
    type: Type
    span: Span


@dataclass
class Target:
    name: str
    args: list[str]


@dataclass
class Instruction:
    op: str
    dest: Param | None
    args: list[Any]
    span: Span


@dataclass
class Block:
    name: str
    params: list[Param]
    instructions: list[Instruction]
    span: Span


@dataclass
class Function:
    name: str
    params: list[Param]
    returns: Type
    blocks: list[Block]
    span: Span
    external: bool = False
    exported: bool = False


@dataclass
class Struct:
    name: str
    fields: list[Param]
    span: Span


@dataclass
class Data:
    name: str
    value: bytes
    span: Span


@dataclass
class Module:
    name: str
    version: int
    functions: list[Function]
    structs: list[Struct]
    data: list[Data]
    source: str
    filename: str

    def function(self, name: str) -> Function:
        return next(f for f in self.functions if f.name == name.lstrip("@"))
