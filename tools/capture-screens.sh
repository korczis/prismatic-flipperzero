#!/usr/bin/env bash
# Re-capture the README gallery from the real device. Two long walks (designed in the simulator,
# which tools/parity.sh proves pixel-identical) start the app with the loader, walk fixed keys
# and keep chosen frames; few restarts, because many loader cycles in a row have wedged the CLI; docs/evidence/pocket/manifest.json lists them with
# captions and the firmware read from the device. tools/docs.py renders the gallery from it.
#   tools/capture-screens.sh        (the app and the pack must already be on the device)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/docs/evidence/pocket"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
dev="$root/tools/device.sh"

relaunch() {
  "$dev" cli "loader close" >/dev/null || true # nothing running is fine
  "$dev" cli "loader open /ext/apps/Tools/prismatic.fap" >/dev/null
  sleep 6 # pack open, whole-pack CRC and index scan: 3551 ms + scan on the 21 MB pack
}

walk() { # name keys [settle]
  relaunch
  "$dev" walk "$tmp/$1" "$2" --settle "${3:-1.2}" >/dev/null
}

# Both walks are designed in the simulator, whose frames tools/parity.sh proves identical; the
# frame numbers below are those walks' steps (NN-<key>.png).
walk tour "right,left,ok,down,ok,back,back,right,ok,ok,ok,down,ok,left,ok,right,ok:long,right,down,back,right,ok:long,ok,back,ok:long,down,down,ok,up,back,back,back,back:long,down,ok"
walk learn "down,right,ok,ok,ok,ok,back,down,ok,left,back,back:long,down,down,ok,right,right,right,right,back,right,ok"

mkdir -p "$out"
rm -f "$out"/*.png
keep() { cp "$tmp/$1" "$out/$2"; }
keep tour/00-start.png 01-home.png
keep tour/01-right.png 02-home-detect.png
keep tour/03-ok.png 03-hub-library.png
keep tour/05-ok.png 04-chapters.png
keep tour/09-ok.png 05-hub-detect.png
keep tour/10-ok.png 06-detectors.png
keep tour/11-ok.png 07-article.png
keep tour/12-down.png 08-chips.png
keep tour/13-ok.png 09-detector.png
keep tour/18-right.png 10-result.png
keep tour/19-down.png 11-result-legend.png
keep tour/22-ok-long.png 12-menu.png
keep tour/23-ok.png 13-contents.png
keep tour/28-ok.png 14-map.png
keep tour/29-up.png 15-map-focus.png
keep tour/35-ok.png 16-hub-act.png
keep learn/03-ok.png 17-hub-train.png
keep learn/04-ok.png 18-quiz.png
keep learn/05-ok.png 19-options.png
keep learn/06-ok.png 20-answer.png
keep learn/09-ok.png 21-trainer.png
keep learn/10-left.png 22-trainer-answer.png
keep learn/15-ok.png 23-info.png
keep learn/19-right.png 24-info-cache.png
keep learn/22-ok.png 25-about.png

firmware="$("$dev" cli "device_info" | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
python3 - "$out/manifest.json" "$firmware" <<'PY'
import json, sys, datetime
captions = [
    ("01-home.png", "Home: six tiles with counts; the status line explains the focused one"),
    ("02-home-detect.png", "Right moves along a row of the grid; the hint follows the focus"),
    ("03-hub-library.png", "Library: find by name, chapters, kinds, everything, a random article"),
    ("04-chapters.png", "The book's chapters with square-root share bars"),
    ("05-hub-detect.png", "Detect: every detector, or a random one"),
    ("06-detectors.png", "Every object with markers to check, paged by forty"),
    ("07-article.png", "An article: bold title, the kind in a pill, its code"),
    ("08-chips.png", "Chips: detector markers and links on the map, one press away"),
    ("09-detector.png", "Detector: progress cells, one marker at a time: No, Unsure, Yes"),
    ("10-result.png", "Result: your marks as a number and a bar, never a score"),
    ("11-result-legend.png", "The bar's legend, then what to rule out and the next steps"),
    ("12-menu.png", "Hold OK: the page's actions over the dimmed page"),
    ("13-contents.png", "Contents: every section; OK jumps there"),
    ("14-map.png", "Map of links: four neighbours at a time, confusables dotted"),
    ("15-map-focus.png", "A focused neighbour's full name in the header; OK walks there"),
    ("16-hub-act.png", "Act: what to do, and pairs to tell apart"),
    ("17-hub-train.png", "Train: two kinds of question and objects to quiz"),
    ("18-quiz.png", "Quiz: which term is this? The name is masked"),
    ("19-options.png", "Distractors are the objects the page calls confusable"),
    ("20-answer.png", "The answer, the definition, a link to the article"),
    ("21-trainer.png", "Trainer: manipulative or benign? A catalogue example"),
    ("22-trainer-answer.png", "Why, and a good response, from the catalogue"),
    ("23-info.png", "Pack and device: source, contents, measured times"),
    ("24-info-cache.png", "The read cache on the device: hits and misses"),
    ("25-about.png", "About: what the app is and the keys"),
]
json.dump({"captured": datetime.date.today().isoformat(), "firmware": sys.argv[2],
           "command": "tools/capture-screens.sh",
           "frames": [{"file": f, "caption": c} for f, c in captions]},
          open(sys.argv[1], "w"), indent=1)
open(sys.argv[1], "a").write("\n")
PY
echo "captured $(find "$out" -name '*.png' | wc -l | tr -d ' ') frames into $out"
