#!/usr/bin/env python3
"""Differential fuzz: the C reader and the Python reference must accept and reject exactly the
same mutated packs. Mutations are re-sealed (CRC recomputed) so the structural checks decide.

Usage: tests/native/differential.py <test_pkp binary> <pack> <workdir> [rounds]
"""

import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import pocket  # noqa: E402


def reseal(d):
    """Recompute the index CRC (everything after the header except the string pool), so the
    structural checks, not the checksum, decide. A damaged directory falls back to whole-file."""
    n = struct.unpack_from("<H", d, 16)[0]
    so, sl = len(d), 0
    for k in range(min(n, 32)):
        at = 32 + 16 * k
        if at + 16 > len(d):
            break
        t, _e, off, length, _c = struct.unpack_from("<HHIII", d, at)
        if t == 2 and 32 <= off and off + length <= len(d):
            so, sl = off, length
    crc = zlib.crc32(bytes(d[so + sl:]), zlib.crc32(bytes(d[32:so])))
    d[12:16] = struct.pack("<I", crc)


def main(argv):
    binary, pack, work = argv[0], Path(argv[1]).read_bytes(), Path(argv[2])
    rounds = int(argv[3]) if len(argv) > 3 else 1500
    work.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261125)
    names, expected = [], {}
    for i in range(rounds):
        d = bytearray(pack)
        for _ in range(1 + i % 5):
            at = rng.randrange(32, min(len(d), 32 + 16 * 8)) if i % 2 else rng.randrange(len(d))
            d[at] = rng.randrange(256)
        reseal(d)
        name = f"m{i:05d}.pkp"
        (work / name).write_bytes(d)
        try:
            pocket.decode(bytes(d))
            expected[name] = "ok"
        except (pocket.PocketError, struct.error, IndexError):
            expected[name] = "err"
        names.append(name)
    (work / "list.txt").write_text("\n".join(names) + "\n")
    out = subprocess.run([binary, "walk", str(work)], capture_output=True, text=True, check=True)
    got = dict(line.split() for line in out.stdout.splitlines())
    diff = [n for n in names if got.get(n) != expected[n]]
    oks = sum(v == "ok" for v in expected.values())
    print(f"differential: {rounds} mutated packs, {oks} accepted, {rounds - oks} rejected, "
          f"{len(diff)} disagreements")
    for n in diff[:10]:
        print(f"  {n}: python {expected[n]}, C {got.get(n)}", file=sys.stderr)
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
