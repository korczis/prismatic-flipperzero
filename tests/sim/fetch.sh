#!/usr/bin/env bash
# Fetch the u8g2 sources the Flipper firmware itself builds (lib/u8g2 at the pinned firmware
# commit) into build/sim/u8g2 and verify every file against tests/sim/u8g2.sha256. The host
# simulator draws with exactly the renderer and fonts that run on the device.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
commit=9e810dc399e027e48ce07751df72168abbb19065 # flipperzero-firmware tag 1.5.1
dir="$root/build/sim/u8g2"
mkdir -p "$dir"
cd "$dir"
if shasum -a 256 -c "$root/tests/sim/u8g2.sha256" >/dev/null 2>&1; then exit 0; fi
while read -r _ f; do
  curl -sfL --retry 3 -o "$f" \
    "https://raw.githubusercontent.com/flipperdevices/flipperzero-firmware/$commit/lib/u8g2/$f"
done <"$root/tests/sim/u8g2.sha256"
shasum -a 256 -c "$root/tests/sim/u8g2.sha256" >/dev/null
echo "u8g2 @ firmware $commit verified"
