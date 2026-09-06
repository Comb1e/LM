import json
from pathlib import Path
import sys


def main():
    try:
        doc = json.loads(Path("examples/stdlib/input.json").read_text())
        if not isinstance(doc, dict):
            return 1
        if "count" not in doc:
            return 4
        if type(doc["count"]) is not int:
            return 6
        if not -(1 << 63) <= doc["count"] < (1 << 63) - 1:
            return 2
        doc["count"] += 1
        output = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
        Path("build/stdlib-transformed.json").write_text(output)
        sys.stdout.write(output)
        return 0
    except (ValueError, UnicodeError):
        return 6
    except OSError:
        return 7


if __name__ == "__main__":
    raise SystemExit(main())
