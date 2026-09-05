from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time

from .config import DEFAULTS
from .emit import emit_c
from .model import CompileError, Diagnostic, Module, Span, Type
from .ops import OPS
from .parser import parse
from .verify import verify


@dataclass
class ProcessResult:
    exit_code: int
    stdout: str
    stderr: str
    timed_out: bool = False
    output_limited: bool = False


def execute(command: list[str], timeout: float, output_limit: int) -> ProcessResult:
    """Drain both pipes with bounded capture and kill the process group on limits."""
    output = {"stdout": bytearray(), "stderr": bytearray()}
    timed_out = output_limited = False
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               stdin=subprocess.DEVNULL, start_new_session=True)
    deadline = time.monotonic() + timeout
    try:
        with selectors.DefaultSelector() as selector:
            for name in output:
                selector.register(getattr(process, name), selectors.EVENT_READ, name)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    timed_out = True
                    break
                for key, _ in selector.select(min(remaining, 0.1)):
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    used = sum(map(len, output.values()))
                    output[key.data].extend(chunk[:max(0, output_limit - used)])
                    if used + len(chunk) > output_limit:
                        output_limited = True
                        break
                if output_limited:
                    break
            if not timed_out and not output_limited:
                try:
                    process.wait(timeout=max(0.001, deadline - time.monotonic()))
                except subprocess.TimeoutExpired:
                    timed_out = True
    finally:
        # Children inheriting neither pipe must not survive a completed run either.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        process.stdout.close()
        process.stderr.close()
    return ProcessResult(process.returncode, output["stdout"].decode("utf-8", "replace"),
                         output["stderr"].decode("utf-8", "replace"), timed_out, output_limited)


def read_module(path: str | Path, config: dict | None = None, check=True) -> Module:
    config = config or DEFAULTS
    path = Path(path)
    with path.open("rb") as stream:
        raw = stream.read(config["limits"]["source_bytes"] + 1)
    if len(raw) > config["limits"]["source_bytes"]:
        raise CompileError([Diagnostic("E_LIMIT", "parse", "Source exceeds configured byte limit")])
    module = parse(raw.decode("utf-8"), str(path), config)
    return verify(module, config) if check else module


def atomic_write(path: str | Path, text: str):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".lm0-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
        os.chmod(temporary, path.stat().st_mode & 0o777 if path.exists() else 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def backend_errors(result: ProcessResult) -> list[Diagnostic]:
    if result.timed_out or result.output_limited:
        return [Diagnostic("E_BACKEND_LIMIT", "backend", "Compiler exceeded time or output limit")]
    diagnostics = []
    try:
        messages = json.loads(result.stderr)
    except ValueError:
        messages = []
    if isinstance(messages, list):
        for item in messages:
            if not isinstance(item, dict) or item.get("kind") not in {"error", "fatal error"}:
                continue
            span = None
            locations = item.get("locations", [])
            if locations:
                caret = locations[0].get("caret", {})
                if "file" in caret:
                    span = Span(caret["file"], caret.get("line", 1), caret.get("column", 1),
                                caret.get("line", 1), caret.get("column", 1))
            diagnostics.append(Diagnostic("E_BACKEND", "backend", item.get("message", "Compilation failed"), span))
    return diagnostics or [Diagnostic("E_BACKEND", "backend", result.stderr.strip() or "Compiler failed")]


def build_c(source: str, output: str | Path, *, kind="exe", optimization=None,
            sanitize=False, links=(), libraries=(), config=None) -> dict:
    config = config or DEFAULTS
    cc = config["compiler"]["cc"]
    timeout = config["compiler"]["build_timeout_seconds"]
    limit = config["limits"]["output_bytes"]
    target = execute([cc, "-dumpmachine"], timeout, limit)
    if target.exit_code or target.timed_out or target.stdout.strip() != config["compiler"]["target"]:
        raise CompileError([Diagnostic("E_TARGET", "backend", "Unsupported compiler target",
                                       expected=config["compiler"]["target"], actual=target.stdout.strip())])
    if kind not in {"exe", "object"}:
        raise ValueError("Build kind must be exe or object")
    optimization = optimization or config["compiler"]["optimization"]
    if optimization not in {"0", "1", "2", "3", "s"}:
        raise ValueError("Invalid optimization level")
    if kind == "object" and (links or libraries):
        raise ValueError("Object builds do not link extra inputs or libraries")
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".lm0-build-", dir=output.parent) as temporary:
        root = Path(temporary)
        c_path = root / "module.c"
        c_path.write_text(source, encoding="utf-8")
        binary = root / "result"
        command = [cc, "-std=c11", "-O" + optimization, "-fno-fast-math", "-ffp-contract=off",
                   "-fexcess-precision=standard", "-fdiagnostics-format=json", "-Wall", "-Wextra",
                   "-Wno-unused-variable", "-Wno-unused-function", "-Wno-unused-label",
                   "-Wno-unused-parameter", str(c_path), "-o", str(binary)]
        if sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie"]
            if kind == "exe":
                command.append("-no-pie")
        if kind == "object":
            command.append("-c")
        else:
            command.extend(str(Path(link).resolve(strict=True)) for link in links)
            command.extend("-l" + library for library in libraries)
            command.append("-lm")
        result = execute(command, timeout, limit)
        if result.exit_code or result.timed_out or result.output_limited:
            raise CompileError(backend_errors(result))
        os.replace(binary, output)
    return {"output": str(output), "kind": kind, "optimization": optimization, "sanitized": sanitize}


def build(module: Module, output: str | Path, **options) -> dict:
    config = options.get("config") or DEFAULTS
    source = emit_c(module, executable=options.get("kind", "exe") == "exe", config=config)
    return build_c(source, output, **options)


def function_signature(function):
    return {"name": function.name, "params": [{"name": p.name, "type": str(p.type)} for p in function.params],
            "returns": str(function.returns), "external": function.external, "exported": function.exported}


def select(module: Module, function_name: str, block_name: str | None = None):
    try:
        function = module.function(function_name)
    except StopIteration:
        raise CompileError([Diagnostic("E_FUNCTION", "tool", "Function not found", actual=function_name)])
    target = function
    if block_name:
        target = next((b for b in function.blocks if b.name == block_name.lstrip("^")), None)
        if target is None:
            raise CompileError([Diagnostic("E_BLOCK", "tool", "Block not found", actual=block_name)])
    return function, target


def inspect(module: Module, function_name: str, block_name: str | None = None) -> dict:
    function, target = select(module, function_name, block_name)
    blocks = [target] if block_name else function.blocks
    instructions = [i for b in blocks for i in b.instructions]
    calls = {i.args[0] for i in instructions if i.op == "call"}
    ops = sorted({i.op for i in instructions})
    targets = {t.name for i in instructions for t in (i.args if i.op == "jump" else i.args[1:] if i.op == "branch" else [])}
    relevant_blocks = [b for b in function.blocks if not block_name or b is target or b.name in targets]
    callees = [f for f in module.functions if f.name in calls]
    needed = set()
    structs = {s.name: s for s in module.structs}

    def require_type(type_):
        if type_.element:
            require_type(type_.element)
        elif type_.name in structs and type_.name not in needed:
            needed.add(type_.name)
            for field in structs[type_.name].fields:
                require_type(field.type)

    for f in [function, *callees]:
        require_type(f.returns)
        for param in f.params:
            require_type(param.type)
    for b in relevant_blocks:
        for param in b.params:
            require_type(param.type)
    for ins in instructions:
        if ins.dest:
            require_type(ins.dest.type)
        for arg in ins.args:
            if isinstance(arg, Type):
                require_type(arg)
    data_names = {i.args[0] for i in instructions if i.op == "address"}
    return {"module": module.name, "version": module.version, "function": function_signature(function),
            "source": module.source[target.span.start:target.span.end],
            "blocks": [{"name": b.name, "params": [{"name": p.name, "type": str(p.type)} for p in b.params]}
                       for b in relevant_blocks],
            "callees": [function_signature(f) for f in callees],
            "instructions": {name: asdict(OPS[name]) for name in ops},
            "types": [{"name": s.name, "fields": [{"name": p.name, "type": str(p.type)} for p in s.fields]}
                      for s in module.structs if s.name in needed],
            "data": [{"name": d.name, "bytes": len(d.value), "source": module.source[d.span.start:d.span.end]}
                     for d in module.data if d.name in data_names]}


def replace(module: Module, function_name: str, replacement: str, output: str | Path,
            block_name: str | None = None, config=None) -> Module:
    function, target = select(module, function_name, block_name)
    fragment_source = "module fragment version 1\n" + (
        "fn @fragment() -> void {\n" + replacement + "\n}\n" if block_name else replacement + "\n")
    fragment = parse(fragment_source, "<replacement>", config)
    if (fragment.structs or fragment.data or len(fragment.functions) != 1 or
            (block_name and (len(fragment.functions[0].blocks) != 1 or
                            fragment.functions[0].blocks[0].name != target.name)) or
            (not block_name and fragment.functions[0].name != function.name)):
        raise CompileError([Diagnostic("E_REPLACE", "tool", "Replacement must contain exactly the selected declaration")])
    source = module.source[:target.span.start] + replacement.rstrip() + "\n" + module.source[target.span.end:]
    updated = verify(parse(source, str(output), config), config)
    updated_function, _ = select(updated, function.name, block_name)
    if ([f.name for f in updated.functions] != [f.name for f in module.functions] or
            (block_name and [b.name for b in updated_function.blocks] != [b.name for b in function.blocks])):
        raise CompileError([Diagnostic("E_REPLACE", "tool", "Replacement must preserve declaration identities")])
    atomic_write(output, source)
    return updated
