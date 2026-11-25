#!/usr/bin/env python3
"""Derived regions of the Markdown docs: regenerate them, or fail when one has drifted.

A region sits between `<!-- docs:begin NAME -->` and `<!-- docs:end NAME -->`; its text is
derived, never typed. Generators:

  pack-stats   tables from docs/data/pack-stats.json (the pack build report, snapshotted)
  pack-kinds   object kinds with counts and share bars, from the same snapshot
  screens      the real device frames listed in docs/evidence/pocket/manifest.json
  timeline     the repository's commits (hash, time, subject) from git

  tools/docs.py            rewrite every region in README.md and docs/*.md
  tools/docs.py --check    exit 1 listing each drifted region (CI, verify, pre-commit); the
                           timeline only has to be a prefix of the history
  tools/docs.py --snapshot <report.json>   refresh docs/data/pack-stats.json from a build
"""

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATS = ROOT / "docs" / "data" / "pack-stats.json"
SCREENS = ROOT / "docs" / "evidence" / "pocket" / "manifest.json"
REGION = re.compile(r"(<!-- docs:begin (?P<name>[a-z-]+) -->\n)(?P<body>.*?)(<!-- docs:end (?P=name) -->)",
                    re.S)


def fmt(n):
    return f"{n:,}".replace(",", " ")


def pack_stats():
    s = json.loads(STATS.read_text())
    quiz = s["quiz"]["definition"] + s["quiz"]["trainer"]
    rows = [
        ("Knowledge objects", s["objects"]),
        ("Categories", s["categories"]),
        ("Typed relations (links between objects)", s["relations"]),
        ("Objects with a detector (observable markers)", s["capabilities"]["detect"]),
        ("Objects with what-to-do steps", s["capabilities"]["apply"]),
        ("Objects with confusables to rule out", s["capabilities"]["compare"]),
        ("Quiz questions", quiz),
        ("  of which: recognise the term from its definition", s["quiz"]["definition"]),
        ("  of which: manipulative or benign? (catalogue examples)", s["quiz"]["trainer"]),
        ("Markers (indicators)", s["items"]["indicators"]),
        ("Actions and escalation steps", s["items"]["actions"] + s["items"]["escalation"]),
        ("Chapters", s["items"]["heading"]),
    ]
    out = ["| | |", "|---|---:|"]
    out += [f"| {label} | **{fmt(value)}** |" for label, value in rows]
    out.append("")
    out.append(f"Pack `{s['pack']}`: {fmt(s['bytes'])} bytes, sha256 `{s['sha256'][:16]}…`, "
               "rebuilt byte-for-byte identical by CI from the pinned source.")
    return "\n".join(out) + "\n"


def pack_kinds():
    s = json.loads(STATS.read_text())
    total = sum(s["kinds"].values())
    out = ["| Kind | Objects | Share |", "|---|---:|---|"]
    for kind, n in sorted(s["kinds"].items(), key=lambda kv: (-kv[1], kv[0])):
        bar = "█" * max(1, round(24 * n / total))
        out.append(f"| `{kind}` | {fmt(n)} | `{bar}` {100 * n / total:.1f} % |")
    return "\n".join(out) + "\n"


def screens():
    m = json.loads(SCREENS.read_text())
    cells = []
    for shot in m["frames"]:
        cells.append(f'<td align="center" width="50%"><img src="docs/evidence/pocket/{shot["file"]}" '
                     f'width="384" alt="{shot["caption"]}"><br><sub>{shot["caption"]}</sub></td>')
    rows = ["<table>"]
    for i in range(0, len(cells), 2):
        rows.append("  <tr>" + "".join(cells[i:i + 2]) + "</tr>")
    rows.append("</table>")
    rows.append("")
    rows.append(f"Captured {m['captured']} on firmware {m['firmware']} with `{m['command']}`.")
    return "\n".join(rows) + "\n"


def timeline():
    log = subprocess.run(["git", "-C", str(ROOT), "log", "--reverse",
                          "--format=%h|%ad|%s", "--date=format:%H:%M"],
                         check=True, capture_output=True, text=True).stdout
    out = ["| Commit | Time | What landed |", "|---|---|---|"]
    for line in log.splitlines():
        h, t, subject = line.split("|", 2)
        out.append(f"| `{h}` | {t} | {subject} |")
    return "\n".join(out) + "\n"


GENERATORS = {"pack-stats": pack_stats, "pack-kinds": pack_kinds, "screens": screens,
              "timeline": timeline}


def documents():
    return [ROOT / "README.md"] + sorted((ROOT / "docs").glob("*.md"))


def current(name, body, cache):
    """A region is current when it equals its generator's output. The timeline may lag by the
    commits made after it was written (a commit cannot list itself), but every row it shows must
    be a real commit, in order: it has to be a prefix of the history."""
    if name != "timeline":
        return body == cache[name]
    have, want = body.splitlines(), cache[name].splitlines()
    return len(have) >= 2 and have == want[: len(have)]


def render(text, path, cache):
    def sub(m):
        name = m.group("name")
        if name not in GENERATORS:
            raise SystemExit(f"{path.name}: unknown region {name!r}")
        if name not in cache:
            cache[name] = GENERATORS[name]()
        return m.group(1) + cache[name] + m.group(4)

    return REGION.sub(sub, text)


def main(argv):
    if argv[:1] == ["--snapshot"]:
        report = json.loads(Path(argv[1]).read_text())
        STATS.parent.mkdir(parents=True, exist_ok=True)
        STATS.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
        print(f"snapshot: {STATS.relative_to(ROOT)}")
        return 0
    check = "--check" in argv
    cache, drifted = {}, []
    for path in documents():
        text = path.read_text()
        new = render(text, path, cache)
        if new == text:
            continue
        if check:
            drifted += [f"{path.relative_to(ROOT)}: {m.group('name')}"
                        for m in REGION.finditer(text)
                        if not current(m.group("name"), m.group("body"), cache)]
        else:
            path.write_text(new)
            print(f"updated {path.relative_to(ROOT)}")
    if drifted:
        print("derived doc regions drifted (run: tools/docs.py):\n  " + "\n  ".join(drifted),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
