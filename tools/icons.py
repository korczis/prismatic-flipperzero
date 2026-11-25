#!/usr/bin/env python3
"""Derive the app's monochrome icons from geometry and write them as 1-bit PNGs.

The icons are an original geometric mark: a prism with one incoming beam split into three
outgoing rays. Nothing is hand-drawn in a binary editor; this script is the source and the
PNGs under app/ are its output. `--check` fails when the committed PNGs drift.

Usage: tools/icons.py [--check] [--preview]
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 10x10 menu icon, drawn at its native size because geometry does not survive 10 pixels.
APP_ICON = [
    "....#.....",
    "...#.#....",
    "...#.#...#",
    "#####.#.#.",
    "..#...##..",
    "..#....###",
    ".#.....#..",
    ".#######.#",
    "..........",
    "..........",
]


def line(grid, x0, y0, x1, y1):
    """Bresenham line into a set of (x, y) pixels."""
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        grid.add((x0, y0))
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def logo(size=40):
    """The prism mark at `size` x `size`: triangle, beam in, three rays out."""
    s = size - 1
    px = set()
    apex, left, right = (s // 2, 2), (4, s - 4), (s - 4, s - 4)
    for a, b in ((apex, left), (left, right), (right, apex)):
        line(px, *a, *b)
    hit = (s // 2 - 7, s // 2 + 2)  # where the beam meets the left face
    line(px, 0, s // 2 + 8, *hit)
    exit_ = (s // 2 + 6, s // 2)
    for dy in (-9, 0, 9):
        line(px, *exit_, s, s // 2 + dy - 4)
    line(px, *hit, *exit_)
    return [
        "".join("#" if (x, y) in px else "." for x in range(size)) for y in range(size)
    ]


def png(rows):
    """Encode rows of '#'/'.' as a 1-bit grayscale PNG, '#' black on white."""
    width, height = len(rows[0]), len(rows)
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter: none
        bits = 0
        for i, ch in enumerate(row):
            bits = (bits << 1) | (0 if ch == "#" else 1)
            if i % 8 == 7:
                raw.append(bits)
                bits = 0
        if width % 8:
            raw.append(bits << (8 - width % 8))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    header = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


TARGETS = {
    ROOT / "app" / "prismatic.png": APP_ICON,
    ROOT / "app" / "images" / "prism_40x40.png": logo(40),
}


def main(argv):
    if "--preview" in argv:
        for path, rows in TARGETS.items():
            print(path.relative_to(ROOT))
            print("\n".join(rows))
        return 0
    drift = []
    for path, rows in TARGETS.items():
        data = png(rows)
        if "--check" in argv:
            if not path.exists() or path.read_bytes() != data:
                drift.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    if drift:
        print("icons drifted from tools/icons.py: " + ", ".join(drift), file=sys.stderr)
        print("fix: python3 tools/icons.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
