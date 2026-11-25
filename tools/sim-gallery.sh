#!/usr/bin/env bash
# Render the interface gallery with the host simulator: the real pack, the firmware's own u8g2
# and fonts, the same app code as the FAP. tools/parity.sh proves the device draws the same
# pixels. docs/evidence/sim/manifest.json lists the frames for tools/docs.py.
#   tools/sim-gallery.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
out="docs/evidence/sim"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
tests/sim/build.sh
run() { # name keys
  mkdir -p "$tmp/$1"
  build/sim/pocketsim build/packs/kompendium.pkp "$tmp/$1" "$2" >/dev/null
}
run home ""
run article "down,ok,ok,down"
run section "down,ok,ok,right"
run menu "down,ok,ok,ok:long,ok"
run map "down,ok,ok,ok:long,down,down,ok,up"
run detect "down,ok,ok,down,ok,left,ok,right,ok:long,right"
run quiz "down,down,down,down,ok,ok,ok"
run trainer "down,down,down,down,down,ok,left"
run browse "ok"
mkdir -p "$out"
rm -f "$out"/*.png
python3 - "$tmp" "$out" <<'PY'
import datetime, json, sys
sys.path.insert(0, "tools")
import flipper_screen as f
tmp, out = sys.argv[1:3]
frames = [
    ("home/00-start.bin", "01-home.png", "Home: icons, counts and a bar for each share of the pack"),
    ("browse/01-ok.bin", "02-browse.png", "Browse: the book's chapters, sized by a square-root bar"),
    ("article/03-ok.bin", "03-article.png", "An article: bold title, the kind in a pill, its code"),
    ("article/04-down.bin", "04-chips.png", "One Down reaches the action chips: detector, quiz, map"),
    ("section/04-right.bin", "05-section.png", "Right jumps a section; heading and icon move into the header"),
    ("menu/04-ok-long.bin", "06-menu.png", "Hold OK: the page's actions over a stippled page"),
    ("menu/05-ok.bin", "07-contents.png", "Contents: every section; OK jumps there"),
    ("map/07-ok.bin", "08-map.png", "Map of links: confusables dotted, four neighbours at a time"),
    ("map/08-up.bin", "09-map-focus.png", "A focused neighbour's full name in the header; OK walks there"),
    ("detect/05-ok.bin", "10-detector.png", "Detector: progress cells, one marker at a time, No / ? / Yes"),
    ("detect/10-right.bin", "11-result.png", "Result: your marks as a number and a bar, never a score"),
    ("quiz/05-ok.bin", "12-quiz.png", "Quiz: which term is this? The name is masked"),
    ("quiz/06-ok.bin", "13-options.png", "Distractors are the objects the page calls confusable"),
    ("quiz/07-ok.bin", "14-answer.png", "The answer, the definition, a link to the article"),
    ("trainer/06-ok.bin", "15-trainer.png", "Trainer: manipulative or benign? A catalogue example"),
    ("trainer/07-left.bin", "16-trainer-answer.png", "Why, and a good response, from the catalogue"),
]
for src, dst, _ in frames:
    data = open(f"{tmp}/{src}", "rb").read()
    open(f"{out}/{dst}", "wb").write(f.png(f.frame_to_rows(data), 4))
json.dump({"captured": datetime.date.today().isoformat(), "firmware": "simulator (u8g2 of 1.5.1)",
           "command": "tools/sim-gallery.sh",
           "frames": [{"file": d, "caption": c} for _, d, c in frames]},
          open(f"{out}/manifest.json", "w"), indent=1)
open(f"{out}/manifest.json", "a").write("\n")
PY
echo "sim gallery: $(find "$out" -name '*.png' | wc -l | tr -d ' ') frames in $out"
