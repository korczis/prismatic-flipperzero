#!/usr/bin/env bash
# Every operation that talks to the physical Flipper goes through here, one at a time:
# a lock directory (build/.device.lock, owner PID inside) serialises device ownership, so two
# shells never deploy to the same device at once. Nothing here flashes firmware.
#   tools/device.sh launch [sdk]        build, install and start the app (ufbt launch)
#   tools/device.sh deploy [sdk]        install build/<sdk>/prismatic.fap without starting it
#   tools/device.sh deploy-pack [pack]  copy a knowledge pack to SD:/apps_data/prismatic/
#   tools/device.sh remove-pack <name>  remove a test pack (acceptance*.pkp) put there by tooling
#   tools/device.sh screenshot <png>    one real frame from the RPC screen stream
#   tools/device.sh walk <dir> <keys>   frames before and after each key, e.g. right,back
#   tools/device.sh cli "<command>"     one Flipper CLI command (loader info, uptime, ...)
#   tools/device.sh bridge <cmd> ...    tools/bridge.py over RPC: status, push-pack, pull
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
lock="$root/build/.device.lock"
mkdir -p "$root/build"
if ! mkdir "$lock" 2>/dev/null; then
  owner="$(cat "$lock/pid" 2>/dev/null || true)"
  if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
    echo "device: busy, PID $owner owns the Flipper ($lock)" >&2
    exit 15
  fi
  echo "device: removing stale lock of exited PID ${owner:-unknown}" >&2
  rm -rf "$lock"
  mkdir "$lock"
fi
echo $$ > "$lock/pid"
trap 'rm -rf "$lock"' EXIT

shopt -s nullglob
ports=(/dev/cu.usbmodemflip_* /dev/serial/by-id/*Flipper*)
if [ "${#ports[@]}" -eq 0 ]; then
  echo "device: no Flipper serial port. Connect it over a data cable and leave DFU mode;" >&2
  echo "        close qFlipper if it is running (it holds the port)." >&2
  exit 12
fi
pys=("$HOME"/.ufbt/toolchain/*/bin/python3)
[ "${#pys[@]}" -gt 0 ] || { echo "device: uFBT toolchain python not found; run: just sdk" >&2; exit 12; }
py="${pys[0]}"

cmd="${1:-}"; shift || true
case "$cmd" in
  launch)
    # Build, install over the storage CLI and start with the loader. Not `ufbt launch`: an
    # app started over RPC is closed when any later RPC session (a screenshot) ends.
    sdk="${1:-official}"
    "$root/tools/build.sh" "$sdk"
    timeout 60 "$py" "$root/tools/flipper_screen.py" cli "loader close" >/dev/null || true  # nothing running is fine
    timeout 120 "$py" "$HOME/.ufbt/current/scripts/storage.py" -p auto \
      send "$root/build/$sdk/prismatic.fap" /ext/apps/Tools/prismatic.fap
    timeout 60 "$py" "$root/tools/flipper_screen.py" cli "loader open /ext/apps/Tools/prismatic.fap"
    ;;
  cli) timeout 60 "$py" "$root/tools/flipper_screen.py" cli "$@" ;;
  deploy)
    sdk="${1:-official}"
    fap="$root/build/$sdk/prismatic.fap"
    [ -s "$fap" ] || { echo "device: $fap missing; run: just build $sdk" >&2; exit 12; }
    timeout 120 "$py" "$HOME/.ufbt/current/scripts/storage.py" -p auto \
      send "$fap" /ext/apps/Tools/prismatic.fap
    ;;
  deploy-pack)
    pack="${1:-$root/build/packs/kompendium.pkp}"
    [ -s "$pack" ] || { echo "device: $pack missing; run: just pack" >&2; exit 12; }
    timeout 900 "$py" "$HOME/.ufbt/current/scripts/storage.py" -p auto mkdir /ext/apps_data/prismatic >/dev/null 2>&1 \
      || true  # mkdir reports an error when the directory exists; send below fails loudly if it is unusable
    timeout 900 "$py" "$HOME/.ufbt/current/scripts/storage.py" -p auto \
      send "$pack" "/ext/apps_data/prismatic/$(basename "$pack")"
    ;;
  remove-pack)
    # only a pack this tooling put there for a test (acceptance*.pkp), never the user's packs
    name="${1:-}"
    case "$name" in acceptance*.pkp) ;; *) echo "device: remove-pack takes acceptance*.pkp only" >&2; exit 2 ;; esac
    timeout 120 "$py" "$HOME/.ufbt/current/scripts/storage.py" -p auto \
      remove "/ext/apps_data/prismatic/$name"
    ;;
  screenshot) timeout 60 "$py" "$root/tools/flipper_screen.py" capture "$@" ;;
  walk) timeout 120 "$py" "$root/tools/flipper_screen.py" walk "$@" ;;
  bridge) timeout 1800 "$py" "$root/tools/bridge.py" "$@" ;;  # a 20 MB push takes minutes
  *) sed -n '2,12p' "$0" >&2; exit 2 ;;
esac
