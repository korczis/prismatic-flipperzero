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

walk detect "down,ok,ok,right,ok,right,right,left,ok,right,down,down,down" 2.0
walk terms "down,down,down,down,ok,ok,ok"
walk manip "down,down,down,down,down,ok,left"
walk browse "ok,ok"
walk info "down,down,down,down,down,down,ok,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down,down" 0.6

mkdir -p "$out"
rm -f "$out"/*.png
keep() { cp "$tmp/$1" "$out/$2"; }
keep detect/00-start.png 01-home.png
keep detect/02-ok.png 02-detectors.png
keep detect/04-right.png 04-article-link.png
keep detect/05-ok.png 05-detector.png
keep detect/10-right.png 06-result.png
keep detect/13-down.png 07-result-steps.png
keep terms/05-ok.png 08-quiz-term.png
keep terms/06-ok.png 09-quiz-options.png
keep terms/07-ok.png 10-quiz-result.png
keep manip/06-ok.png 11-trainer.png
keep manip/07-left.png 12-trainer-result.png
keep browse/01-ok.png 13-categories.png
keep browse/02-ok.png 14-objects.png
keep info/07-ok.png 15-info.png
keep info/33-down.png 16-info-measured.png

firmware="$("$dev" cli "device_info" | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
python3 - "$out/manifest.json" "$firmware" <<'PY'
import json, sys, datetime
captions = [
    ("01-home.png", "Home: every count is derived from the pack on the SD card"),
    ("02-detectors.png", "Detectors: objects whose page lists observable markers"),
    ("04-article-link.png", "Right jumps to the next link; OK opens it"),
    ("05-detector.png", "Detector: one marker at a time, No / ? / Yes, hold OK for n/a"),
    ("06-result.png", "Result: your own marks back, never a score"),
    ("07-result-steps.png", "Then what to rule out first and the next steps"),
    ("08-quiz-term.png", "Quiz: which term is this? The name is masked in the definition"),
    ("09-quiz-options.png", "Distractors come from the objects the page calls confusable"),
    ("10-quiz-result.png", "The answer, with the full definition and a link to the article"),
    ("11-trainer.png", "Trainer: manipulative or benign? A catalogue example in context"),
    ("12-trainer-result.png", "Why, and a good response, from the catalogue"),
    ("13-categories.png", "Browse: the Kompendium's own chapters and collections"),
    ("14-objects.png", "A category, paged; Back returns to the same row"),
    ("15-info.png", "Pack and device info: source commit, contents"),
    ("16-info-measured.png", "Provenance: the source repository and the pinned commit"),
]
json.dump({"captured": datetime.date.today().isoformat(), "firmware": sys.argv[2],
           "command": "tools/capture-screens.sh",
           "frames": [{"file": f, "caption": c} for f, c in captions]},
          open(sys.argv[1], "w"), indent=1)
open(sys.argv[1], "a").write("\n")
PY
echo "captured $(find "$out" -name '*.png' | wc -l | tr -d ' ') frames into $out"
