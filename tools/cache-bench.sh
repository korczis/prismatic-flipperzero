#!/usr/bin/env bash
# Measure what the read cache saves: the device-tour walk of tools/capture-screens.sh over the
# real pack in the host simulator, once through the cache and once reading the card directly
# (POCKET_NOCACHE=1). The simulator counts every read that reaches the "card". Writes
# docs/evidence/cache/bench.json, which tools/docs.py renders.
#   tools/cache-bench.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
tests/sim/build.sh >/dev/null
keys="$(sed -n 's/^walk tour "\(.*\)"$/\1/p' tools/capture-screens.sh)"
[ -n "$keys" ] || { echo "cache-bench: no tour walk in tools/capture-screens.sh" >&2; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/c" "$tmp/n"
build/sim/pocketsim build/packs/kompendium.pkp "$tmp/c" "$keys" 2>"$tmp/cached.txt" >/dev/null
POCKET_NOCACHE=1 build/sim/pocketsim build/packs/kompendium.pkp "$tmp/n" "$keys" \
  2>"$tmp/direct.txt" >/dev/null
mkdir -p docs/evidence/cache
python3 - "$tmp/cached.txt" "$tmp/direct.txt" "$keys" <<'PY'
import json, re, sys
def stats(path):
    m = re.search(r"reads: (\d+) card reads, (\d+) bytes; cache (\d+) hits, (\d+) misses",
                  open(path).read())
    return dict(zip(("card_reads", "bytes", "hits", "misses"), map(int, m.groups())))
cached, direct = stats(sys.argv[1]), stats(sys.argv[2])
json.dump({"command": "tools/cache-bench.sh", "walk": "tour of tools/capture-screens.sh",
           "keys": sys.argv[3], "steps": sys.argv[3].count(",") + 1,
           "cache": {"blocks": 8, "block_bytes": 256},
           "cached": cached, "direct": direct},
          open("docs/evidence/cache/bench.json", "w"), indent=1)
open("docs/evidence/cache/bench.json", "a").write("\n")
print(f"cache-bench: {direct['card_reads']} card reads without the cache, "
      f"{cached['card_reads']} with it")
PY
