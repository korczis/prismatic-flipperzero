#!/usr/bin/env bash
# Measure what the read cache saves: the device-tour walk of tools/capture-screens.sh over the
# real pack in the host simulator, once through the cache and once reading the card directly
# (POCKET_NOCACHE=1). The simulator counts every read that reaches the "card". Then Find by
# name, through the cache: opening it (the index check included), every first letter (the worst
# is kept), a typical third letter ("fal"), turning the wheel and Left. Writes
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
python3 - "$tmp" <<'PY'
import json, re, subprocess, sys
sys.path.insert(0, "tools")
import pocket
WHEEL = "abcdefghijklmnopqrstuvwxyz0123456789"
pack = "build/packs/kompendium.pkp"
dump = pocket.decode(open(pack, "rb").read())
keys = [e["key"] for e in dump["search"]]
def offered(q):
    return sorted(pocket.search(dump, q, keys)["next"], key=WHEEL.index)
def typing(word):
    out, q = [], ""
    for c in word:
        out += ["down"] * offered(q).index(c) + ["right"]
        q += c
    return out
def steps(walk):
    run = f"{sys.argv[1]}/find"
    subprocess.run(["rm", "-rf", run], check=True)
    subprocess.run(["mkdir", "-p", run], check=True)
    err = subprocess.run(["build/sim/pocketsim", pack, run, ",".join(walk)], capture_output=True,
                         text=True, check=True).stderr
    return [int(n) for n in re.findall(r"^step \d+ \S+: (\d+) card reads", err, re.M)][2:]
first = {c: steps(["ok", "ok"] + typing(c))[-1] for c in offered("")}
worst = max(first, key=first.get)
walk = ["ok", "ok"] + typing("fal") + ["down", "left"]
cost = steps(walk)
bench = json.load(open("docs/evidence/cache/bench.json"))
bench["find"] = {"open": cost[1], "first_letter_worst": first[worst], "worst_letter": worst,
                 "first_letter_median": sorted(first.values())[len(first) // 2],
                 "third_letter": cost[-3], "third_query": "fal", "wheel_turn": cost[-2],
                 "left": cost[-1], "block_bytes": 256}
json.dump(bench, open("docs/evidence/cache/bench.json", "w"), indent=1)
open("docs/evidence/cache/bench.json", "a").write("\n")
print(f"cache-bench: Find opens in {cost[1]} card reads; worst first letter {worst!r} "
      f"{first[worst]}; third letter of fal {cost[-3]}; wheel {cost[-2]}; Left {cost[-1]}")
PY
