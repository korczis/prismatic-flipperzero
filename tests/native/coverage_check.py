#!/usr/bin/env python3
"""Fail listing every line of <dir>/*.c that has code and never ran.

  coverage_check.py <coverage.lcov> [dir, default app/core]

Reads `llvm-cov export -format=lcov`: its DA records are llvm-cov's own line coverage (the
counts `llvm-cov show` prints), so a closing `break;` or `return;` that only carries an empty
gap region is not reported as a line that never ran.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def parse(text):
    files, current = {}, None
    for line in text.splitlines():
        if line.startswith("SF:"):
            current = files.setdefault(Path(line[3:]).resolve(), {})
        elif line.startswith("DA:") and current is not None:
            number, count = line[3:].split(",")[:2]
            current[int(number)] = max(current.get(int(number), 0), int(count))
    return files


def main(path, folder="app/core"):
    missing, seen = [], 0
    for name, lines in sorted(parse(Path(path).read_text()).items()):
        if name.parent != ROOT / folder or name.suffix != ".c":
            continue
        seen += 1
        lost = sorted(n for n, c in lines.items() if c == 0)
        rel = name.relative_to(ROOT)
        print(f"{rel}: {len(lines) - len(lost)}/{len(lines)} lines with code ran")
        missing += [f"{rel}:{n}" for n in lost]
    if seen == 0:
        sys.exit(f"coverage: no {folder} sources in the report")
    if missing:
        print("coverage: lines no test ran:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1
    print(f"coverage: every line with code in {folder}/*.c ran")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
