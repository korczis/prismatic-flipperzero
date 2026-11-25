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
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("POCKET_ROOT") or Path(__file__).resolve().parent.parent)
STATS = ROOT / "docs" / "data" / "pack-stats.json"
SCREENS = ROOT / "docs" / "evidence" / "pocket" / "manifest.json"
ATLAS = ROOT / "docs" / "evidence" / "atlas" / "manifest.json"
CACHE = ROOT / "docs" / "evidence" / "cache" / "bench.json"
UI_APP = ROOT / "app" / "ui" / "ui_app.c"
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


def gallery(manifest, folder, note):
    """A two-column table of frames from a manifest, with where they came from."""
    m = json.loads(manifest.read_text())
    cells = []
    for shot in m["frames"]:
        cells.append(f'<td align="center" width="50%"><img src="{folder}/{shot["file"]}" '
                     f'width="384" alt="{shot["caption"]}"><br><sub>{shot["caption"]}</sub></td>')
    rows = ["<table>"]
    for i in range(0, len(cells), 2):
        rows.append("  <tr>" + "".join(cells[i:i + 2]) + "</tr>")
    rows.append("</table>")
    rows.append("")
    rows.append(note.format(**m))
    return "\n".join(rows) + "\n"


def screens():
    return gallery(SCREENS, "docs/evidence/pocket",
                   "Captured {captured} on firmware {firmware} with `{command}`.")


def atlas():
    return gallery(ATLAS, "docs/evidence/atlas",
                   "Rendered by `{command}` from `{spec}`: the real pack, the firmware's own u8g2 "
                   "and fonts, the same app code as the FAP. Every screen of the app has a frame, "
                   "and CI fails when one is missing or stale ([rule](.ai/repo/rules/project/"
                   "ui-atlas.v1.md)); `tools/parity.sh` recorded the device drawing identical "
                   "frames ([evidence](docs/evidence/parity/manifest.json)).")


def cache_bench():
    """The read cache, measured: the same walk with and without it."""
    b = json.loads(CACHE.read_text())
    c, d = b["cached"], b["direct"]
    asked = c["hits"] + c["misses"]
    fewer = round(100 * (d["card_reads"] - c["card_reads"]) / d["card_reads"])
    more = round(100 * (c["bytes"] - d["bytes"]) / d["bytes"])
    return "\n".join([
        "| The same walk | Card reads | Bytes from the card |",
        "|---|---:|---:|",
        f"| Reading the card directly | {fmt(d['card_reads'])} | {fmt(d['bytes'])} |",
        f"| Through the cache ({b['cache']['blocks']} x {b['cache']['block_bytes']} B) "
        f"| {fmt(c['card_reads'])} | {fmt(c['bytes'])} |",
        "",
        f"The cache served {fmt(c['hits'])} of {fmt(asked)} block requests from memory: "
        f"{fewer}% fewer card reads for {more:+d}% bytes, since a miss reads a whole block. "
        f"Walk: {b['steps']} key presses ({b['walk']}); measured by `{b['command']}`.",
    ]) + "\n"


def ia_tree():
    """The information architecture as a Mermaid tree, read from the hubs table in ui_app.c."""
    src = UI_APP.read_text()
    table = src[src.index("static const Hub hubs[]"):src.index("#define HUB_COUNT")]
    out = ["```mermaid", "flowchart LR", '  home(["Home"])']
    hubs = re.findall(r'\{UiIcon\w+,\s*"([^"]+)",\s*Home\w+,\s*"[^"]*",\s*\{(.*?)\}\}', table,
                      re.S)
    for h, (name, body) in enumerate(hubs):
        out.append(f'  home --> h{h}["{name}"]')
        entries = re.findall(r'\{UiIcon\w+,\s*Home\w+,\s*"([^"]+)",\s*"[^"]*",\s*"([^"]+)"',
                             body)
        for e, (label, hint) in enumerate(entries):
            out.append(f'  h{h} --> h{h}e{e}["{label}<br/><small>{hint}</small>"]')
    for k, name in enumerate(re.findall(r'page_add_tile\(app->page, UiIcon\w+, "([^"]+)"', src)):
        out.append(f'  home --> t{k}["{name}"]')
    out.append("```")
    return "\n".join(out) + "\n"


def timeline():
    log = subprocess.run(["git", "-C", str(ROOT), "log", "--reverse",
                          "--format=%h|%ad|%s", "--date=format:%H:%M"],
                         check=True, capture_output=True, text=True).stdout
    out = ["| Commit | Time | What landed |", "|---|---|---|"]
    for line in log.splitlines():
        h, t, subject = line.split("|", 2)
        out.append(f"| `{h}` | {t} | {subject} |")
    return "\n".join(out) + "\n"


UI_SOURCE = ROOT / "app" / "ui" / "page.c"
UI_MEANING = {
    "SCREEN_W": "display width", "SCREEN_H": "display height",
    "HEADER_H": "pinned header (title, position, rule)", "SOFT_H": "softkey bar",
    "LEFT": "left text margin", "RIGHT": "last text column (scrollbar right of it)",
    "LEADING": "body line pitch", "ROW_H": "list row", "HEADING_H": "section heading bar",
    "BIG_H": "big-number result line", "BAR_H": "stacked bar with legend",
    "GAP": "space after a paragraph", "TEXT_CAP": "text per page", "BLOCK_CAP": "blocks per page",
    "LINE_CAP": "laid-out lines per page",
}


def ui_geometry():
    """The design system's numbers, read from the engine's own #defines."""
    rows = ["| Constant | Value | Meaning |", "|---|---:|---|"]
    for name, value in re.findall(r"^#define (\w+)\s+(\d+)u?\b", UI_SOURCE.read_text(), re.M):
        if name in UI_MEANING:
            unit = "" if name.endswith("_CAP") else " px"
            rows.append(f"| `{name}` | {value}{unit} | {UI_MEANING[name]} |")
    return "\n".join(rows)


GENERATORS = {"atlas": atlas, "cache-bench": cache_bench, "ia-tree": ia_tree, "ui-geometry": ui_geometry, "pack-stats": pack_stats, "pack-kinds": pack_kinds, "screens": screens,
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
