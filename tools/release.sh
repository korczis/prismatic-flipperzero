#!/usr/bin/env bash
# Collect built FAPs into build/release/ with names that carry version, firmware and API, plus
# each build's provenance JSON and one SHA256SUMS over everything. Refuses a dirty or missing
# build: a release asset must come from a committed tree.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(cat "$root/VERSION")"
out="$root/build/release"
rm -rf "$out"
mkdir -p "$out"
n=0
while IFS=$'\t' read -r id _v _status; do
  meta="$root/build/$id/build.json"
  [ -f "$meta" ] || { echo "release: skip $id (not built)"; continue; }
  python3 - "$meta" "$version" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
if m["version"] != sys.argv[2]:
    sys.exit(f"release: {sys.argv[1]} is version {m['version']}, VERSION says {sys.argv[2]}")
if m["source_dirty"] or not m["source_commit"]:
    sys.exit(f"release: {sys.argv[1]} was built from an uncommitted tree; rebuild after commit")
PY
  api="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["api"])' "$meta")"
  name="prismatic-${version}-${id}-api${api}"
  cp "$root/build/$id/prismatic.fap" "$out/$name.fap"
  cp "$meta" "$out/$name.build.json"
  n=$((n + 1))
done < <(python3 "$root/tools/sdk.py" list)
[ "$n" -gt 0 ] || { echo "release: nothing built" >&2; exit 12; }
if [ -f "$root/build/packs/kompendium.pkp" ]; then
  cp "$root/build/packs/kompendium.pkp" "$out/kompendium-${version}.pkp"
  cp "$root/build/packs/kompendium.report.json" "$out/kompendium-${version}.report.json"
fi
(
  cd "$out"
  shopt -s nullglob
  assets=(*.fap *.json *.pkp)
  shasum -a 256 "${assets[@]}" > SHA256SUMS
  shasum -a 256 -c SHA256SUMS
)
ls -l "$out"
