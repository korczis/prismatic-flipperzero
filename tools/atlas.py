#!/usr/bin/env python3
"""The screen atlas: one frame of every screen and view of the app, from the host simulator.

  tools/atlas.py           render tests/sim/atlas.txt into docs/evidence/atlas
  tools/atlas.py --check   render into a temporary folder and fail unless
                           - every walk ends on the screen (and overlay) it declares,
                           - every Screen of app/ui/ui_app.c appears at least once,
                           - docs/evidence/atlas equals the fresh render: the same files,
                             the same manifest, the same pixels in every PNG (compared after
                             decompression, so another zlib cannot fail the check).

The simulator draws with the firmware's own u8g2 and fonts and the same app code as the FAP
(tools/parity.sh proves the device draws identical pixels), so the atlas is what the device
shows. Everything is deterministic: the pack, the seeded random numbers, the renderer.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flipper_screen as fs  # noqa: E402

ROOT = Path(os.environ.get("POCKET_ROOT") or Path(__file__).resolve().parents[1])
SPEC = ROOT / "tests" / "sim" / "atlas.txt"
OUT = ROOT / "docs" / "evidence" / "atlas"
SIM = ROOT / "build" / "sim" / "pocketsim"
UI_APP = ROOT / "app" / "ui" / "ui_app.c"
SHEET_COLS = 4


def screens():
    """Every screen of ui_app.c: the X(...) entries of its SCREENS list."""
    body = re.search(r"#define SCREENS\(X\)(.*?)\n\n", UI_APP.read_text(), re.S).group(1)
    return re.findall(r"X\((\w+)\)", body)


def parse(text):
    entries = []
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, pack, keys, view, caption = (f.strip() for f in line.split("|"))
        entries.append({"name": name, "pack": pack, "keys": keys, "view": view,
                        "caption": caption})
    return entries


def packs(tmp):
    """Pack paths by spec name: the real pack (compiled if missing) and the test packs."""
    real = ROOT / "build" / "packs" / "kompendium.pkp"
    if not real.exists():
        subprocess.run([sys.executable, "tools/pocket.py", "compile"], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, "tests/sim/packs.py", str(tmp)], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return {"kompendium": str(real), "ui": str(Path(tmp) / "ui.pkp"), "-": "-"}


def last_view(dump):
    """The screen and overlay the walk's last frame shows."""
    frames = dump.split("\n== ")
    last = frames[-1].splitlines()
    screen = next(line[2:] for line in last if line.startswith("@ "))
    overlay = "+menu" if "[Actions]" in last else "+contents" if "[Contents]" in last else ""
    return screen + overlay


def render(out):
    out.mkdir(parents=True, exist_ok=True)
    entries = parse(SPEC.read_text())
    problems = []
    frames = []
    with tempfile.TemporaryDirectory() as tmp:
        paths = packs(tmp)
        for i, e in enumerate(entries, 1):
            run = Path(tmp) / e["name"]
            run.mkdir()
            dump = subprocess.run([str(SIM), paths[e["pack"]], str(run), e["keys"]],
                                  capture_output=True, text=True, check=True).stdout
            seen = last_view(dump)
            if seen != e["view"]:
                problems.append(f"{e['name']}: ends on {seen}, declares {e['view']}")
            last = sorted(run.glob("[0-9]*.bin"))[-1]
            rows = fs.frame_to_rows(last.read_bytes())
            frames.append(rows)
            e["file"] = f"{i:02d}-{e['name']}.png"
            e["screen"] = e["view"].split("+")[0]
            (out / e["file"]).write_bytes(fs.png(rows, 4))
    # a contact sheet: every frame at 2x, four to a row, 4 px gutters
    rows_n = (len(frames) + SHEET_COLS - 1) // SHEET_COLS
    width, height = SHEET_COLS * 132 - 4, rows_n * 68 - 4
    sheet = [[False] * width for _ in range(height)]
    for k, rows in enumerate(frames):
        ox, oy = (k % SHEET_COLS) * 132, (k // SHEET_COLS) * 68
        for y, row in enumerate(rows):
            sheet[oy + y][ox:ox + 128] = row
    (out / "sheet.png").write_bytes(fs.png(sheet, 2))
    manifest = {"command": "tools/atlas.py", "spec": "tests/sim/atlas.txt",
                "screens": screens(), "frames": entries}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False) + "\n")
    missing = sorted(set(screens()) - {e["screen"] for e in entries})
    problems += [f"screen {m} has no frame in tests/sim/atlas.txt" for m in missing]
    return problems


def pixels(data):
    """A PNG's decompressed image data: equal pixels compare equal whatever zlib wrote them."""
    if not data.startswith(b"\x89PNG"):
        return data
    at, idat = 8, b""
    while at < len(data):
        n = int.from_bytes(data[at:at + 4], "big")
        if data[at + 4:at + 8] == b"IDAT":
            idat += data[at + 8:at + 8 + n]
        at += 12 + n
    return data[16:24] + zlib.decompress(idat)  # width, height, then the scanlines


def main(argv):
    subprocess.run(["tests/sim/build.sh"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    if argv[:1] != ["--check"]:
        if OUT.exists():
            shutil.rmtree(OUT)
        problems = render(OUT)
        for p in problems:
            print(f"atlas: {p}", file=sys.stderr)
        print(f"atlas: {len(list(OUT.glob('[0-9]*.png')))} frames in {OUT.relative_to(ROOT)}")
        return 1 if problems else 0
    with tempfile.TemporaryDirectory() as tmp:
        fresh = Path(tmp) / "atlas"
        problems = render(fresh)
        have = {p.name: pixels(p.read_bytes()) for p in OUT.glob("*")} if OUT.exists() else {}
        want = {p.name: pixels(p.read_bytes()) for p in fresh.glob("*")}
        for name in sorted(set(have) | set(want)):
            if have.get(name) != want.get(name):
                problems.append(f"docs/evidence/atlas/{name} is stale: run tools/atlas.py")
    for p in problems:
        print(f"atlas: {p}", file=sys.stderr)
    if not problems:
        print(f"atlas: {len(want) - 2} frames current, every screen covered")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
