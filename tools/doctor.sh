#!/usr/bin/env bash
# Readiness report, one line per concern, in five separate groups:
#   host toolchain, SDK, GitHub, device, Majordomus.
# Exit 1 only when something a software build needs is missing. A missing Flipper is
# BLOCKED (hardware verification cannot run), never a build failure; pass
# --require-device to make it one.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
require_device=0
[ "${1:-}" = "--require-device" ] && require_device=1
fail=0
line() { printf '%-8s %-11s %s\n' "$1" "$2" "$3"; }
need() {
  if command -v "$1" >/dev/null 2>&1; then line READY host "$1 ($(command -v "$1"))"
  else line MISSING host "$1 — $2"; fail=1; fi
}

need python3 "required by tools/"
need ufbt "pipx install ufbt"
need git "required"
if command -v cargo >/dev/null 2>&1; then line READY host "cargo $(cargo --version | cut -d' ' -f2)"
else line OPTIONAL host "cargo — only the host bridge needs it"; fi

while IFS=$'\t' read -r id version status; do
  if python3 "$root/tools/sdk.py" check "$id" >/dev/null 2>&1; then
    line READY sdk "$id $version ($status)"
  elif [ "$status" = official ]; then
    line MISSING sdk "$id $version — just sdk $id"; fail=1
  else
    line OPTIONAL sdk "$id $version not installed — just sdk $id"
  fi
done < <(python3 "$root/tools/sdk.py" list)

if command -v gh >/dev/null 2>&1 && login="$(gh api user --jq .login 2>/dev/null)"; then
  line READY github "authenticated as $login"
else
  line OPTIONAL github "gh not authenticated — only publishing needs it"
fi

shopt -s nullglob
ports=(/dev/cu.usbmodemflip_* /dev/serial/by-id/*Flipper*)
if [ "${#ports[@]}" -gt 0 ]; then
  line READY device "Flipper serial port: ${ports[*]}"
else
  line BLOCKED device "no Flipper on USB (looked for /dev/cu.usbmodemflip_*, /dev/serial/by-id/*Flipper*)"
  [ "$require_device" = 1 ] && fail=1
fi
if pgrep -x qFlipper >/dev/null 2>&1; then
  line WARN device "qFlipper is running and may hold the serial port"
fi

if command -v majordomus >/dev/null 2>&1; then
  line READY majordomus "$(majordomus --version)"
  [ -f "$root/.ai/manifest.yaml" ] && line READY majordomus ".ai/ layer present (run: majordomus doctor)"
else
  line MISSING majordomus "https://majordomus.dev/install.sh"; fail=1
fi
exit "$fail"
