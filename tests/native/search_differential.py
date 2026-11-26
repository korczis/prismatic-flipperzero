#!/usr/bin/env python3
"""Search differential: Find by name on the device (app/core/search.c, through test_pkp search)
types every prefix of one to three letters the wheel offers, and each must give exactly what the
reference (tools/pocket.py search) gives: the same matches in the same order, the same strong
count, the same next letters, and the same state again after Left.

Usage: tests/native/search_differential.py <test_pkp binary> <pack>
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import pocket  # noqa: E402

WHEEL = "abcdefghijklmnopqrstuvwxyz0123456789"  # app/core/search.h PKP_SEARCH_LETTERS


def wheel(letters):
    return "".join(sorted(letters, key=WHEEL.index))


def expected(dump):
    lines = [f"* {wheel(pocket.search(dump, '')['next'])}"]

    def walk(prefix):
        here = pocket.search(dump, prefix)
        for c in wheel(here["next"]) if len(prefix) < 3 else "":
            found = pocket.search(dump, prefix + c)
            lines.append(f"> {prefix + c} {len(found['results'])} {len(found['strong'])} "
                         f"{wheel(found['next'])} :" + "".join(f" {i}" for i in found["results"]))
            walk(prefix + c)
            lines.append(f"< {prefix} {len(here['results'])} {len(here['strong'])}")

    walk("")
    return lines


def main(argv):
    binary, pack = argv[0], Path(argv[1])
    got = subprocess.run([binary, "search", str(pack)], capture_output=True, text=True,
                         check=True).stdout.splitlines()
    want = expected(pocket.decode(pack.read_bytes()))
    prefixes = sum(line.startswith(">") for line in want)
    diff = [(i, a, b) for i, (a, b) in enumerate(zip(want, got)) if a != b]
    if len(got) != len(want):
        diff.append((min(len(got), len(want)), f"{len(want)} lines", f"{len(got)} lines"))
    print(f"search differential: {prefixes} prefixes of 1-3 letters, {len(diff)} disagreements")
    for i, a, b in diff[:5]:
        print(f"  line {i}: python {a[:100]!r}\n          C      {b[:100]!r}", file=sys.stderr)
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
