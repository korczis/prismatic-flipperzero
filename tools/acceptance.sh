#!/usr/bin/env bash
# The architectural acceptance test: a new knowledge object needs no change to the app.
#   1. add one canonical object (tests/acceptance/object.json) to the extracted IR;
#   2. recompile only the knowledge pack;
#   3. rebuild the FAP from the unchanged app/ and require the same sha256 as before;
#   4. find the object through the generic UI (Library > By chapter > its chapter > its row),
#      run its detector and answer its quiz in the host simulator, asserting what is shown;
#   5. with --device: deploy the pack as acceptance.pkp (the app opens the first pack by name),
#      walk the same keys on the Flipper, require frames identical to the simulator's, keep the
#      device frames as evidence, and remove acceptance.pkp again.
# With --device it records docs/evidence/acceptance/manifest.json and two device frames.
#   tools/acceptance.sh [--device]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
device=0
[ "${1:-}" = "--device" ] && device=1
out=build/acceptance
rm -rf "$out"
mkdir -p "$out/sim" "$out/dev"

git diff --quiet -- app || { echo "acceptance: app/ has uncommitted changes" >&2; exit 1; }
[ -f build/packs/kompendium.ir.json ] || python3 tools/pocket.py extract >/dev/null
tools/build.sh official >/dev/null
before="$(shasum -a 256 build/official/prismatic.fap | cut -d' ' -f1)"

# 1-2: the IR plus one object, compiled into a pack of its own
python3 - "$out/ir.json" <<'PY'
import json, sys
ir = json.load(open("build/packs/kompendium.ir.json"))
obj = json.load(open("tests/acceptance/object.json"))
assert obj["id"] not in {o["id"] for o in ir["objects"]}, "fixture id already in the IR"
ir["objects"].append(obj)
json.dump(ir, open(sys.argv[1], "w"), ensure_ascii=False)
PY
python3 tools/pocket.py compile --ir "$out/ir.json" --out "$out/acceptance.pkp" \
  --report "$out/report.json" >/dev/null

# 3: the app is untouched and builds to the same bytes
tools/build.sh official >/dev/null
after="$(shasum -a 256 build/official/prismatic.fap | cut -d' ' -f1)"
git diff --quiet -- app || { echo "acceptance: building changed app/" >&2; exit 1; }
[ "$before" = "$after" ] || { echo "acceptance: FAP changed ($before -> $after)" >&2; exit 1; }

# 4: the keys that reach the object through the generic UI, derived from the pack itself
keys="$(python3 - "$out/acceptance.pkp" <<'PY'
import json, subprocess, sys
pack = json.loads(subprocess.run([sys.executable, "tools/pocket.py", "decode", sys.argv[1]],
                                 check=True, capture_output=True, text=True).stdout)
obj = json.load(open("tests/acceptance/object.json"))
index = next(i for i, o in enumerate(pack["objects"]) if o["id"] == obj["id"])
cat = pack["objects"][index]["category"]
first = min(i for i, o in enumerate(pack["objects"]) if o["category"] == cat)
row, page = (index - first) % 40, (index - first) // 40
quiz = next(q for q in pack["quiz"] if q["kind"] == 1 and q["options"][q["answer"]] == index)
keys = ["ok", "ok"] + ["down"] * cat + ["ok"]          # Library, By chapter, its chapter
keys += ["up", "ok"] * page                           # "Next page >" is the last row
keys += ["down"] * (row + (1 if page else 0)) + ["ok"]  # past "< Previous page" on later pages
keys += ["down", "ok", "right", "right", "left"]      # the detect chip; Yes, Yes, No
keys += ["back", "right", "ok", "ok"]                 # back to its chips; the quiz chip; options
keys += ["down"] * quiz["answer"] + ["ok"]            # the right answer
print(",".join(keys))
PY
)"
tests/sim/build.sh >/dev/null
build/sim/pocketsim "$out/acceptance.pkp" "$out/sim" "$keys" >"$out/sim/out.txt" 2>/dev/null
fail=0
for want in "Time pressure (acceptance" "[No|Unsure|Yes]" "2/3 markers observed" \
  "# Which term?" "# Correct"; do
  grep -qF -- "$want" "$out/sim/out.txt" || { echo "acceptance: simulator never showed '$want'" >&2; fail=1; }
done
[ "$fail" -eq 0 ] || exit 1
echo "acceptance: FAP unchanged ($after); the object was found, detected and quizzed in the simulator"

# 5: the same on the device
same=0 diff=0 fw=""
if [ "$device" -eq 1 ]; then
  python3 - "$out/sim" <<'PY'
import glob, sys
sys.path.insert(0, "tools")
import flipper_screen as f
for b in sorted(glob.glob(sys.argv[1] + "/*.bin")):
    open(b[:-4] + ".png", "wb").write(f.png(f.frame_to_rows(open(b, "rb").read()), 4))
PY
  tools/device.sh cli "loader close" >/dev/null || true # nothing running is fine
  tools/device.sh deploy-pack "$out/acceptance.pkp" >/dev/null
  tools/device.sh cli "loader open /ext/apps/Tools/prismatic.fap" >/dev/null
  sleep 4 # pack open and index scan
  tools/device.sh walk "$out/dev" "$keys" --settle 1.2 >/dev/null
  tools/device.sh cli "loader close" >/dev/null || true
  tools/device.sh remove-pack acceptance.pkp
  for f in "$out/dev"/*.png; do
    if cmp -s "$f" "$out/sim/$(basename "$f")"; then same=$((same + 1)); else diff=$((diff + 1)); fi
  done
  fw="$(tools/device.sh cli device_info | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
  echo "acceptance: device frames identical to the simulator: $same, different: $diff"
  mkdir -p docs/evidence/acceptance
  rm -f docs/evidence/acceptance/*.png
  last="$(find "$out/dev" -name '*.png' | sort | tail -n 1)"
  cp "$last" docs/evidence/acceptance/answer.png
  cp "$(find "$out/dev" -name '*-left.png' | sort | tail -n 1)" docs/evidence/acceptance/result.png
fi
if [ "$device" -eq 1 ]; then
  python3 - "$before" "$after" "$keys" "$same" "$diff" "$fw" <<'PY'
import datetime, hashlib, json, sys
before, after, keys, same, diff, fw = sys.argv[1:7]
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
path = "docs/evidence/acceptance/manifest.json"
json.dump({"date": datetime.date.today().isoformat(), "object": "tests/acceptance/object.json",
           "fap_sha256_before": before, "fap_sha256_after": after,
           "pack_sha256": sha("build/acceptance/acceptance.pkp"), "keys": keys,
           "simulator": "found through Library > By chapter, detector 2/3 markers, quiz correct",
           "device": {"firmware": fw, "frames_identical": int(same),
                      "frames_different": int(diff), "frames": ["result.png", "answer.png"]},
           "command": "tools/acceptance.sh --device"}, open(path, "w"), indent=1)
open(path, "a").write("\n")
PY
fi
[ "$diff" -eq 0 ]
