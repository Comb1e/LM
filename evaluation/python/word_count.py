from collections import Counter
from pathlib import Path
import re


def main():
    try:
        data = Path("examples/stdlib/words.txt").read_bytes().lower()
        words = Counter(word for word in re.split(rb"[\x00-\x20]+", data) if word)
        for word, count in words.items():
            print(f"{word.decode('utf-8')}={count}")
        return 0
    except OSError:
        return 7


if __name__ == "__main__":
    raise SystemExit(main())
