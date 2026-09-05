"""A token-based parser; newlines delimit instructions, not indentation."""

from dataclasses import dataclass
import json
import re

from .config import DEFAULTS
from .model import (Block, CompileError, Data, Diagnostic, Function, Instruction,
                    Module, Param, Span, Struct, Target, Type)
from .ops import OPS


@dataclass
class Token:
    kind: str
    text: str
    span: Span


PATTERN = re.compile(
    r'(?P<space>[ \t\r]+)|(?P<comment>\#[^\n]*)|(?P<nl>\n)|'
    r'(?P<string>"(?:[^"\\\n]|\\.)*")|'
    r'(?P<number>-?(?:0[xX][0-9a-fA-F]+|(?:\d+\.\d*|\d+)(?:[eE][+-]?\d+)?))|'
    r'(?P<ref>[%@^][A-Za-z_][A-Za-z_0-9]*)|'
    r'(?P<word>-?inf\b|[A-Za-z_][A-Za-z_0-9]*)|'
    r'(?P<punct>->|[{}()[\]<>:,;=])'
)


def lex(source: str, filename: str) -> list[Token]:
    tokens = []
    pos, line, column = 0, 1, 1
    while pos < len(source):
        match = PATTERN.match(source, pos)
        if not match:
            span = Span(filename, line, column, line, column + 1, pos, pos + 1)
            raise CompileError([Diagnostic("E_SYNTAX", "parse", "Unexpected character", span,
                                           actual=source[pos])])
        value, kind = match.group(), match.lastgroup
        span = Span(filename, line, column, line, column + len(value), pos, match.end())
        if kind not in {"space", "comment"}:
            tokens.append(Token(kind, value, span))
        if kind == "nl":
            line, column = line + 1, 1
        else:
            column += len(value)
        pos = match.end()
    tokens.append(Token("eof", "", Span(filename, line, column, line, column, pos, pos)))
    return tokens


class Parser:
    def __init__(self, source: str, filename: str, config: dict):
        self.source, self.filename, self.config = source, filename, config
        if len(source.encode("utf-8")) > config["limits"]["source_bytes"]:
            raise CompileError([Diagnostic("E_LIMIT", "parse", "Source exceeds configured byte limit")])
        self.tokens = lex(source, filename)
        self.pos = 0

    @property
    def token(self) -> Token:
        return self.tokens[self.pos]

    def take(self, text: str | None = None, kind: str | None = None) -> Token:
        token = self.token
        if (text is not None and token.text != text) or (kind and token.kind != kind):
            self.fail(f"Expected {text or kind}", expected=text or kind, actual=token.text)
        if token.kind == "eof":
            self.fail("Unexpected end of source")
        self.pos += 1
        return token

    def fail(self, message: str, **details) -> None:
        raise CompileError([Diagnostic("E_SYNTAX", "parse", message, self.token.span, **details)])

    def accept(self, text: str) -> bool:
        if self.token.text == text:
            self.take()
            return True
        return False

    def lines(self) -> None:
        while self.token.kind == "nl":
            self.take()

    def endline(self) -> None:
        if self.token.kind not in {"nl", "eof"}:
            self.fail("Expected end of statement")
        self.lines()

    def span_from(self, start: Span) -> Span:
        end = self.tokens[self.pos - 1].span
        return Span(start.file, start.line, start.column, end.end_line, end.end_column,
                    start.start, end.end)

    def ref(self, prefix: str) -> str:
        token = self.take(kind="ref")
        if not token.text.startswith(prefix):
            raise CompileError([Diagnostic("E_SYNTAX", "parse", f"Expected {prefix} reference", token.span)])
        return token.text[1:]

    def integer(self) -> int:
        token = self.take(kind="number")
        try:
            return int(token.text, 16 if "x" in token.text.lower() else 10)
        except ValueError:
            raise CompileError([Diagnostic("E_SYNTAX", "parse", "Expected integer literal", token.span)])

    def type(self, depth: int = 0) -> Type:
        if depth >= self.config["limits"]["type_depth"]:
            self.fail("Type nesting exceeds configured limit")
        if self.accept("["):
            element = self.type(depth + 1)
            self.take(";")
            count = self.integer()
            self.take("]")
            return Type("array", element, count)
        name = self.take(kind="word").text
        if name == "ptr":
            self.take("<")
            element = self.type(depth + 1)
            self.take(">")
            return Type("ptr", element)
        return Type(name)

    def param(self) -> Param:
        start = self.token.span
        name = self.ref("%")
        self.take(":")
        type_ = self.type()
        return Param(name, type_, self.span_from(start))

    def params(self) -> list[Param]:
        self.take("(")
        params = []
        if not self.accept(")"):
            while True:
                params.append(self.param())
                if not self.accept(","):
                    break
            self.take(")")
        return params

    def registers(self) -> list[str]:
        self.take("(")
        result = []
        if not self.accept(")"):
            while True:
                result.append(self.ref("%"))
                if not self.accept(","):
                    break
            self.take(")")
        return result

    def target(self) -> Target:
        name = self.ref("^")
        return Target(name, self.registers())

    def instruction(self) -> Instruction:
        start = self.token.span
        dest = None
        if self.token.text.startswith("%"):
            dest = self.param()
            self.take("=")
        op_token = self.take(kind="word")
        op = op_token.text
        if op not in OPS:
            raise CompileError([Diagnostic("E_OPCODE", "parse", "Unknown instruction", op_token.span,
                                           actual=op)])
        syntax = OPS[op].syntax
        args = []
        if syntax == "literal":
            if self.token.kind not in {"number", "word"}:
                self.fail("Expected scalar literal")
            args = [self.take().text]
        elif syntax in {"unary", "binary", "ternary"}:
            for i in range({"unary": 1, "binary": 2, "ternary": 3}[syntax]):
                if i:
                    self.take(",")
                args.append(self.ref("%"))
        elif syntax == "call":
            args = [self.ref("@"), self.registers()]
        elif syntax in {"type", "type_count", "type_reg"}:
            args = [self.type()]
            if syntax != "type":
                self.take(",")
                args.append(self.integer() if syntax == "type_count" else self.ref("%"))
        elif syntax == "field":
            args = [self.ref("%")]
            self.take(",")
            args.append(self.take(kind="word").text)
        elif syntax == "symbol":
            args = [self.ref("@")]
        elif syntax == "target":
            args = [self.target()]
        elif syntax == "branch":
            args = [self.ref("%")]
            self.take(",")
            args.append(self.target())
            self.take(",")
            args.append(self.target())
        elif syntax == "optional_reg" and self.token.kind not in {"nl", "eof"}:
            args = [self.ref("%")]
        span = self.span_from(start)
        self.endline()
        return Instruction(op, dest, args, span)

    def function(self, start: Span, external=False, exported=False) -> Function:
        self.take("fn")
        name = self.ref("@")
        params = self.params()
        self.take("->")
        returns = self.type()
        blocks = []
        if not external:
            self.take("{")
            self.endline()
            while not self.accept("}"):
                block_start = self.token.span
                block_name = self.ref("^")
                block_params = self.params() if self.token.text == "(" else []
                self.take(":")
                self.endline()
                instructions = []
                while self.token.text != "}" and not self.token.text.startswith("^"):
                    if self.token.kind == "eof":
                        self.fail("Unterminated function")
                    instructions.append(self.instruction())
                blocks.append(Block(block_name, block_params, instructions, self.span_from(block_start)))
        span = self.span_from(start)
        self.endline()
        return Function(name, params, returns, blocks, span, external, exported)

    def module(self) -> Module:
        self.lines()
        self.take("module")
        name = self.take(kind="word").text
        self.take("version")
        version = self.integer()
        self.endline()
        functions, structs, data = [], [], []
        while self.token.kind != "eof":
            start = self.token.span
            if self.accept("struct"):
                struct_name = self.take(kind="word").text
                self.take("{")
                self.endline()
                fields = []
                while not self.accept("}"):
                    field_start = self.token.span
                    field_name = self.take(kind="word").text
                    self.take(":")
                    fields.append(Param(field_name, self.type(), self.span_from(field_start)))
                    self.endline()
                structs.append(Struct(struct_name, fields, self.span_from(start)))
                self.endline()
            elif self.accept("data"):
                data_name = self.ref("@")
                self.take("=")
                token = self.take(kind="string")
                try:
                    value = json.loads(token.text).encode("utf-8")
                except (ValueError, UnicodeError) as error:
                    raise CompileError([Diagnostic("E_LITERAL", "parse", str(error), token.span)])
                data.append(Data(data_name, value, self.span_from(start)))
                self.endline()
            else:
                external = self.accept("extern")
                if external:
                    self.take("c")
                exported = not external and self.accept("export")
                if exported:
                    self.take("c")
                functions.append(self.function(start, external, exported))
        return Module(name, version, functions, structs, data, self.source, self.filename)


def parse(source: str, filename: str = "<input>", config: dict | None = None) -> Module:
    return Parser(source, filename, config or DEFAULTS).module()
