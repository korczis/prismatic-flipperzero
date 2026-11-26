#!/usr/bin/env python3
"""Search differential: Find by name on the device (app/core/search.c, through test_pkp search)
must give exactly what the reference (tools/pocket.py search) gives, in two walks:

  - every prefix of one to three letters the wheel offers, depth first: the same matches in the
    same order, the same strong count, the same next letters, and all of it again after Left;
  - every key typed in full: counts and next letters at each letter, the matches from the fourth
    letter on (this reaches keys cut to ten characters, such as "internacio").

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


class Reference:
    def __init__(self, dump):
        self.dump = dump
        self.keys = [e["key"] for e in dump["search"]]
        self.cache = {}

    def find(self, q):
        if q not in self.cache:
            self.cache[q] = pocket.search(self.dump, q, self.keys)
        return self.cache[q]

    def line(self, mark, q):
        f = self.find(q)
        return (f"{mark} {q} {len(f['results'])} {len(f['strong'])} {wheel(f['next'])} :"
                + "".join(f" {i}" for i in f["results"]))

    def short(self, q):
        f = self.find(q)
        return f"- {q} {len(f['results'])} {len(f['strong'])} {wheel(f['next'])}"


def expected(dump, depth=3):
    """The lines `test_pkp search <pack> <depth>` must print (depth 0: every key in full)."""
    ref = Reference(dump)
    lines = [f"* {wheel(ref.find('')['next'])}"]

    def walk(prefix):
        for c in wheel(ref.find(prefix)["next"]) if len(prefix) < depth else "":
            lines.append(ref.line(">", prefix + c))
            walk(prefix + c)
            lines.append(ref.line("<", prefix))

    if depth:
        walk("")
    for key in ref.keys if not depth else []:
        for n in range(1, len(key) + 1):
            lines.append(ref.line("=", key[:n]) if n >= 4 else ref.short(key[:n]))
    return lines


def compare(binary, pack, depth):
    """(lines compared, the first disagreements) of one walk over one pack."""
    got = subprocess.run([binary, "search", str(pack), str(depth)], capture_output=True,
                         text=True, check=True).stdout.splitlines()
    want = expected(pocket.decode(Path(pack).read_bytes()), depth)
    diff = [(i, a, b) for i, (a, b) in enumerate(zip(want, got)) if a != b]
    if len(got) != len(want):
        diff.append((min(len(got), len(want)), f"{len(want)} lines", f"{len(got)} lines"))
    return len(want), diff


def main(argv):
    binary, pack = argv[0], argv[1]
    bad = 0
    for depth, what in ((3, "prefixes of 1-3 letters with Left"), (0, "keys typed in full")):
        n, diff = compare(binary, pack, depth)
        print(f"search differential: {n} lines, {what}, {len(diff)} disagreements")
        for i, a, b in diff[:5]:
            print(f"  line {i}: python {a[:100]!r}\n          C      {b[:100]!r}",
                  file=sys.stderr)
        bad += len(diff)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
