#!/usr/bin/env bash
# Build the FAP against one pinned SDK from tools/sdks.json and stage it in build/<id>/.
# Refuses when that SDK is not the one installed: ufbt would otherwise fetch a default SDK
# and the artifact's provenance would be wrong.
#   tools/build.sh [official|momentum]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
id="${1:-official}"
python3 "$root/tools/version.py" --check
python3 "$root/tools/icons.py" --check
if ! python3 "$root/tools/sdk.py" check "$id" >/dev/null; then
  echo "build: SDK '$id' is not installed; run: just sdk $id" >&2
  exit 12
fi
export "$(python3 "$root/tools/sdk.py" env "$id")"
cd "$root/app"
ufbt -s
api="$(grep '^Version,' "$UFBT_HOME/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv" | cut -d, -f3)"
out="$root/build/$id"
mkdir -p "$out"
cp dist/prismatic.fap "$out/prismatic.fap"
version="$(cat "$root/VERSION")"
sha="$(shasum -a 256 "$out/prismatic.fap" | cut -d' ' -f1)"
python3 - "$root" "$id" "$api" "$version" "$sha" > "$out/build.json" <<'PY'
import json, subprocess, sys
root, sdk_id, api, version, sha = sys.argv[1:]
e = next(x for x in json.load(open(f"{root}/tools/sdks.json"))["sdks"] if x["id"] == sdk_id)
ufbt = subprocess.run(["ufbt", "--version"], capture_output=True, text=True).stdout
commit = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True,
                        text=True).stdout.strip() or None
dirty = bool(subprocess.run(["git", "-C", root, "status", "--porcelain", "--", "app"],
                            capture_output=True, text=True).stdout.strip())
print(json.dumps({"app": "prismatic", "version": version, "sdk": sdk_id,
                  "sdk_label": e["label"], "sdk_sha256": e["sha256"], "api": api,
                  "target": "f7", "fap_sha256": sha, "source_commit": commit,
                  "source_dirty": dirty}, indent=2))
PY
echo "build: $out/prismatic.fap  api $api  sha256 $sha"
