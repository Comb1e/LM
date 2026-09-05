"""Command-line entry point for the optional Python benchmark orchestrator."""

import argparse
import json
from pathlib import Path

from .benchmark import export_tasks, grade_responses
from .config import load_config
from .tooling import atomic_write


def parser():
    root = argparse.ArgumentParser(prog="lm0-bench", description="LM0 offline benchmark orchestrator")
    root.add_argument("--config", help="TOML configuration overrides")
    commands = root.add_subparsers(dest="command", required=True)
    export = commands.add_parser("export")
    export.add_argument("directory")
    grade = commands.add_parser("grade")
    grade.add_argument("responses")
    grade.add_argument("--execute", action="store_true")
    grade.add_argument("--sanitize", action="store_true",
                       help="Instrument C candidates and native drivers; LM0 assembly is not instrumented")
    grade.add_argument("-o", "--output")
    return root


def main(argv=None):
    args = parser().parse_args(argv)
    config = load_config(args.config)
    if args.command == "export":
        result = export_tasks(Path(args.directory))
    else:
        result = grade_responses(Path(args.responses), execute_candidates=args.execute,
                                 sanitize=args.sanitize, config=config)
        if args.output:
            atomic_write(args.output, json.dumps(result, indent=2) + "\n")
    print(json.dumps({"ok": True, **result}, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
