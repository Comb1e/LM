import argparse
from dataclasses import asdict
import json
import math
from pathlib import Path
import tempfile

from .config import load_config
from .emit import emit_c
from .model import CompileError, Diagnostic
from .tooling import atomic_write, build, execute, inspect, read_module, replace


def parser():
    root = argparse.ArgumentParser(prog="lm0", description="LM0 typed assembly compiler")
    root.add_argument("--config", help="TOML configuration overrides")
    commands = root.add_subparsers(dest="command", required=True)
    for name in ("check", "emit-c", "build", "run", "inspect", "replace"):
        command = commands.add_parser(name)
        command.add_argument("source")
        if name in {"emit-c", "build", "replace"}:
            command.add_argument("-o", "--output", required=True)
        if name in {"build", "run"}:
            command.add_argument("-O", "--optimization", choices=["0", "1", "2", "3", "s"])
            command.add_argument("--sanitize", action="store_true")
            command.add_argument("--link", action="append", default=[])
            command.add_argument("--library", action="append", default=[])
        if name == "run":
            command.add_argument("--timeout", type=float)
        if name == "build":
            command.add_argument("--kind", choices=["exe", "object", "shared"], default="exe")
        if name == "emit-c":
            command.add_argument("--entry", action="store_true", help="Emit executable main wrapper")
        if name in {"inspect", "replace"}:
            command.add_argument("--function", required=True)
            command.add_argument("--block")
        if name == "replace":
            command.add_argument("--replacement", required=True, help="File containing replacement source")
    benchmark = commands.add_parser("bench")
    benchmark_commands = benchmark.add_subparsers(dest="bench_command", required=True)
    export = benchmark_commands.add_parser("export")
    export.add_argument("directory")
    grade = benchmark_commands.add_parser("grade")
    grade.add_argument("responses")
    grade.add_argument("--execute", action="store_true", help="Compile and execute saved native candidates")
    grade.add_argument("--sanitize", action="store_true")
    grade.add_argument("-o", "--output")
    return root


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        config = load_config(args.config)
        if args.command == "bench":
            from .benchmark import export_tasks, grade_responses
            if args.bench_command == "export":
                result = export_tasks(Path(args.directory))
            else:
                result = grade_responses(Path(args.responses), execute_candidates=args.execute,
                                         sanitize=args.sanitize, config=config)
                if args.output:
                    atomic_write(args.output, json.dumps(result, indent=2) + "\n")
            print(json.dumps({"ok": True, **result}, ensure_ascii=True))
            return 0
        module = read_module(args.source, config, check=args.command != "replace")
        result = {"ok": True}
        if args.command == "check":
            result.update(module=module.name, functions=len(module.functions), diagnostics=[])
        elif args.command == "emit-c":
            atomic_write(args.output, emit_c(module, args.entry, config))
            result["output"] = str(Path(args.output).resolve())
        elif args.command in {"build", "run"}:
            options = {"optimization": args.optimization, "sanitize": args.sanitize,
                       "links": args.link, "libraries": args.library, "config": config}
            if args.command == "build":
                result.update(build(module, args.output, kind=args.kind, **options))
            else:
                timeout = args.timeout if args.timeout is not None else config["limits"]["run_timeout_seconds"]
                if timeout <= 0 or not math.isfinite(timeout):
                    raise ValueError("timeout must be finite and positive")
                with tempfile.TemporaryDirectory(prefix="lm0-run-") as temporary:
                    executable = Path(temporary) / "program"
                    build(module, executable, **options)
                    execution = execute([str(executable)], timeout, config["limits"]["output_bytes"])
                result.update(asdict(execution))
                result["ok"] = not (execution.timed_out or execution.output_limited or execution.exit_code < 0)
                runtime_diagnostics = []
                for line in execution.stderr.splitlines():
                    try:
                        message = json.loads(line)
                        if isinstance(message, dict):
                            runtime_diagnostics.extend(message.get("diagnostics", []))
                    except ValueError:
                        pass
                if runtime_diagnostics:
                    result.update(ok=False, diagnostics=runtime_diagnostics)
                elif args.sanitize and any(marker in execution.stderr for marker in ("Sanitizer", "runtime error:")):
                    result.update(ok=False, diagnostics=[Diagnostic(
                        "E_SANITIZER", "runtime", "Native sanitizer reported a failure").json()])
                print(json.dumps(result))
                return 0 if result["ok"] else 3
        elif args.command == "inspect":
            result.update(inspect(module, args.function, args.block))
        elif args.command == "replace":
            updated = replace(module, args.function, Path(args.replacement).read_text(),
                              args.output, args.block, config)
            result.update(output=str(Path(args.output).resolve()), module=updated.name)
        print(json.dumps(result, ensure_ascii=True))
        return 0
    except CompileError as error:
        print(json.dumps({"ok": False, "diagnostics": [d.json() for d in error.diagnostics]}))
        return 2
    except (OSError, ValueError, UnicodeError, RecursionError) as error:
        diagnostic = Diagnostic("E_TOOL", "tool", str(error))
        print(json.dumps({"ok": False, "diagnostics": [diagnostic.json()]}))
        return 2
