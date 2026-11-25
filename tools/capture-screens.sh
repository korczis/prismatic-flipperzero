#!/usr/bin/env bash
# Re-capture the README gallery from the real device. Each path restarts the app with the loader,
# walks fixed keys and keeps chosen frames; docs/evidence/pocket/manifest.json lists them with
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

walk detect "down,ok,ok,down,ok,left,ok,right,ok:long,right,down,down"
walk sections "down,ok,ok,right,right"
walk terms "down,down,down,down,ok,ok,ok"
walk manip "down,down,down,down,down,ok,left"
walk browse "ok,ok"
walk info "up,up,ok"
walk menu "down,ok,ok,ok:long,ok"
walk map "down,ok,ok,ok:long,down,down,ok,up"

mkdir -p "$out"
rm -f "$out"/*.png
keep() { cp "$tmp/$1" "$out/$2"; }
keep detect/00-start.png 01-home.png
keep detect/02-ok.png 02-detectors.png
keep detect/03-ok.png 03-article.png
keep detect/04-down.png 04-article-action.png
keep sections/04-right.png 05-section.png
keep detect/05-ok.png 06-detector.png
keep detect/10-right.png 07-result.png
keep detect/11-down.png 08-result-legend.png
keep terms/05-ok.png 09-quiz-term.png
keep terms/06-ok.png 10-quiz-options.png
keep terms/07-ok.png 11-quiz-result.png
keep manip/06-ok.png 12-trainer.png
keep manip/07-left.png 13-trainer-result.png
keep browse/01-ok.png 14-categories.png
keep browse/02-ok.png 15-objects.png
keep info/03-ok.png 16-info.png
keep menu/04-ok-long.png 17-menu.png
keep menu/05-ok.png 18-contents.png
keep map/07-ok.png 19-map.png
keep map/08-up.png 20-map-focus.png

firmware="$("$dev" cli "device_info" | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
python3 - "$out/manifest.json" "$firmware" <<'PY'
import json, sys, datetime
captions = [
    ("01-home.png", "Home: icons, and counts derived from the pack on the SD card"),
    ("02-detectors.png", "A list: position in the header, rows that page by four"),
    ("03-article.png", "An article: the category above, the title in bold, kind and code"),
    ("04-article-action.png", "One Down reaches the actions: the detector, the quiz"),
    ("05-section.png", "Right jumps a section; its heading moves into the header"),
    ("06-detector.png", "Detector: progress cells, one marker at a time, No / ? / Yes"),
    ("07-result.png", "Result: your marks as a number and a bar, never a score"),
    ("08-result-legend.png", "The bar's legend, then what to rule out and the next steps"),
    ("09-quiz-term.png", "Quiz: which term is this? The name is masked"),
    ("10-quiz-options.png", "Distractors come from the objects the page calls confusable"),
    ("11-quiz-result.png", "The answer, with the definition and a link to the article"),
    ("12-trainer.png", "Trainer: manipulative or benign? A catalogue example"),
    ("13-trainer-result.png", "Why, and a good response, from the catalogue"),
    ("14-categories.png", "Browse: the Kompendium's chapters and collections"),
    ("15-objects.png", "A category; Back returns to the same row"),
    ("16-info.png", "Pack and device: source, contents, measured open and scan times"),
    ("17-menu.png", "Hold OK: the page's actions in a panel over the page"),
    ("18-contents.png", "Contents: the article's sections; OK jumps there"),
    ("19-map.png", "Map of links: the object and four neighbours, confusables dotted"),
    ("20-map-focus.png", "A focused neighbour's full name in the header; OK walks there"),
]
json.dump({"captured": datetime.date.today().isoformat(), "firmware": sys.argv[2],
           "command": "tools/capture-screens.sh",
           "frames": [{"file": f, "caption": c} for f, c in captions]},
          open(sys.argv[1], "w"), indent=1)
open(sys.argv[1], "a").write("\n")
PY
echo "captured $(find "$out" -name '*.png' | wc -l | tr -d ' ') frames into $out"
