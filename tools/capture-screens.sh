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
  sleep 3 # pack open and index scan, measured at about two seconds
}

walk() { # name keys [settle]
  relaunch
  "$dev" walk "$tmp/$1" "$2" --settle "${3:-1.2}" >/dev/null
}

walk tour "down,ok,ok,down,ok,left,ok,right,ok:long,right,down,back,right,ok,up,back,ok:long,ok,down,ok,back,back"
walk learn "down,down,down,down,ok,ok,ok,back,down,ok,left,back,up,up,up,up,up,ok,ok,back,back,up,up,ok"

mkdir -p "$out"
rm -f "$out"/*.png
keep() { cp "$tmp/$1" "$out/$2"; }
keep tour/00-start.png 01-home.png
keep learn/18-ok.png 02-browse.png
keep tour/02-ok.png 03-list.png
keep tour/03-ok.png 04-article.png
keep tour/04-down.png 05-chips.png
keep tour/20-ok.png 06-section.png
keep tour/17-ok-long.png 07-menu.png
keep tour/18-ok.png 08-contents.png
keep tour/14-ok.png 09-map.png
keep tour/15-up.png 10-map-focus.png
keep tour/05-ok.png 11-detector.png
keep tour/10-right.png 12-result.png
keep tour/11-down.png 13-result-legend.png
keep learn/05-ok.png 14-quiz.png
keep learn/06-ok.png 15-options.png
keep learn/07-ok.png 16-answer.png
keep learn/10-ok.png 17-trainer.png
keep learn/11-left.png 18-trainer-answer.png
keep learn/19-ok.png 19-objects.png
keep learn/24-ok.png 20-info.png

firmware="$("$dev" cli "device_info" | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
python3 - "$out/manifest.json" "$firmware" <<'PY'
import json, sys, datetime
captions = [
    ("01-home.png", "Home: icons, counts and a bar for each share of the pack"),
    ("02-browse.png", "Browse: the book's chapters, sized by a square-root bar"),
    ("03-list.png", "A list: position in the header, pages of forty"),
    ("04-article.png", "An article: bold title, the kind in a pill, its code"),
    ("05-chips.png", "One Down reaches the action chips: detector, map"),
    ("06-section.png", "A section: its heading and icon move into the header"),
    ("07-menu.png", "Hold OK: the page's actions over a stippled page"),
    ("08-contents.png", "Contents: every section; OK jumps there"),
    ("09-map.png", "Map of links: confusables dotted, four neighbours at a time"),
    ("10-map-focus.png", "A focused neighbour's full name in the header; OK walks there"),
    ("11-detector.png", "Detector: progress cells, one marker at a time, No / ? / Yes"),
    ("12-result.png", "Result: your marks as a number and a bar, never a score"),
    ("13-result-legend.png", "The bar's legend, then what to rule out and the next steps"),
    ("14-quiz.png", "Quiz: which term is this? The name is masked"),
    ("15-options.png", "Distractors are the objects the page calls confusable"),
    ("16-answer.png", "The answer, the definition, a link to the article"),
    ("17-trainer.png", "Trainer: manipulative or benign? A catalogue example"),
    ("18-trainer-answer.png", "Why, and a good response, from the catalogue"),
    ("19-objects.png", "A category; Back returns to the same row"),
    ("20-info.png", "Pack and device: source, contents, measured open and scan times"),
]
json.dump({"captured": datetime.date.today().isoformat(), "firmware": sys.argv[2],
           "command": "tools/capture-screens.sh",
           "frames": [{"file": f, "caption": c} for f, c in captions]},
          open(sys.argv[1], "w"), indent=1)
open(sys.argv[1], "a").write("\n")
PY
echo "captured $(find "$out" -name '*.png' | wc -l | tr -d ' ') frames into $out"
