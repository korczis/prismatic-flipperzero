#!/usr/bin/env python3
"""Fail listing every line of app/core/*.c that has code and never ran (llvm-cov export JSON)."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main(path):
    data = json.loads(Path(path).read_text())["data"][0]["files"]
    missing, seen = [], 0
    for f in data:
        name = Path(f["filename"]).resolve()
        if name.parent != ROOT / "app" / "core" or name.suffix != ".c":
            continue
        seen += 1
        lines = {}
        for line, _col, count, has_count, is_entry, *_rest in f["segments"]:
            if has_count:
                lines[line] = max(lines.get(line, 0), count) if is_entry else lines.get(line, count)
        # A line counts as run when any region starting on it ran.
        ran = {}
        for line, _col, count, has_count, *_rest in f["segments"]:
            if has_count:
                ran[line] = ran.get(line, 0) or count
        lost = sorted(ln for ln, c in ran.items() if c == 0)
        total = len(ran)
        print(f"{name.relative_to(ROOT)}: {total - len(lost)}/{total} lines with code ran")
        missing += [f"{name.relative_to(ROOT)}:{ln}" for ln in lost]
    if seen == 0:
        sys.exit("coverage: no app/core sources in the report")
    if missing:
        print("coverage: lines no test ran:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1
    print("coverage: every line with code in app/core/*.c ran")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
