#!/usr/bin/env bash
# Proves the host simulator draws what the device draws: walks the same keys on the Flipper
# (app and pack already installed, app started) and in tests/sim, and compares every frame.
#   tools/parity.sh [keys] [--record]   default keys: the home grid, a hub, a list, an article, chips, menu, map, long Back
# --record writes docs/evidence/parity/manifest.json with the result, firmware, FAP and pack.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
keys="${1:-right,down,up,ok,ok,ok,down,right,ok:long,back,ok:long,down,down,ok,up,back,back,back,back:long,down,ok,down,ok}"
record=0
if [ "${1:-}" = "--record" ]; then
  keys="right,down,up,ok,ok,ok,down,right,ok:long,back,ok:long,down,down,ok,up,back,back,back,back:long,down,ok,down,ok"
  record=1
elif [ "${2:-}" = "--record" ]; then
  record=1
fi
dev="$(mktemp -d build/sim/parity-dev.XXXX)"
sim="$(mktemp -d build/sim/parity-sim.XXXX)"
tests/sim/build.sh
tools/device.sh cli "loader close" >/dev/null || true # nothing running is fine
tools/device.sh cli "loader open /ext/apps/Tools/prismatic.fap" >/dev/null
sleep 6 # pack open, whole-pack CRC and index scan: 3551 ms + scan measured on the 21 MB pack
tools/device.sh walk "$dev" "$keys" --settle 1.2 >/dev/null
build/sim/pocketsim build/packs/kompendium.pkp "$sim" "$keys" >"$sim/out.txt"
python3 - "$sim" <<'PY'
import glob, sys
sys.path.insert(0, "tools")
import flipper_screen as f
for b in sorted(glob.glob(sys.argv[1] + "/*.bin")):
    open(b[:-4] + ".png", "wb").write(f.png(f.frame_to_rows(open(b, "rb").read()), 4))
PY
same=0 diff=0
for f in "$dev"/*.png; do
  if cmp -s "$f" "$sim/$(basename "$f")"; then same=$((same + 1)); else diff=$((diff + 1)); echo "DIFF $(basename "$f")"; fi
done
echo "parity: $same identical, $diff different"
if [ "$record" -eq 1 ]; then
  mkdir -p docs/evidence/parity
  fw="$(tools/device.sh cli device_info | awk -F': ' '/^firmware_version/ {gsub(/ /,"",$2); print $2}')"
  python3 - docs/evidence/parity/manifest.json "$keys" "$same" "$diff" "$fw" <<'PY'
import datetime, hashlib, json, sys
sha = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
out, keys, same, diff, fw = sys.argv[1:6]
json.dump({"date": datetime.date.today().isoformat(), "firmware": fw, "keys": keys,
           "frames_identical": int(same), "frames_different": int(diff),
           "fap_sha256": sha("build/official/prismatic.fap"),
           "pack_sha256": sha("build/packs/kompendium.pkp"),
           "command": "tools/parity.sh --record"}, open(out, "w"), indent=1)
open(out, "a").write("\n")
PY
fi
[ "$diff" -eq 0 ] && [ "$same" -gt 0 ]
