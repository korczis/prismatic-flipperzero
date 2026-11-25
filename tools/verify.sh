#!/usr/bin/env bash
# The one verification entry point: local, CI and `majordomus finish --verify-command`.
# Every step must pass; nothing here masks an exit code.
#   tools/verify.sh            all SDKs whose zip is installed (official is required); needs the
#                              Kompendium source (sibling checkout or KOMPENDIUM_DIR)
#   VERIFY_SKIP_GOVERNANCE=1   CI's build job, where the governance job runs doctor
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
step() { printf '\n== %s\n' "$*"; }

step "generated sources are in sync"
python3 tools/version.py --check
python3 tools/icons.py --check
python3 tools/docs.py --check

step "shell scripts"
if command -v shellcheck >/dev/null 2>&1; then shellcheck tools/*.sh tests/tools/*.sh .githooks/*
else echo "shellcheck not installed; skipped (CI runs it)"; fi

step "tool tests and line coverage (every executable line in tools/*.py)"
python3 tests/tools/coverage_gate.py
tests/tools/coverage_gate_selftest.sh

step "knowledge pack: extract, compile twice, compare bytes"
python3 tools/pocket.py build >/dev/null
python3 tools/pocket.py compile --out build/packs/again.pkp --report build/packs/again.json >/dev/null
cmp build/packs/kompendium.pkp build/packs/again.pkp
echo "pack deterministic: $(shasum -a 256 build/packs/kompendium.pkp | cut -d' ' -f1)"
cmp docs/data/pack-stats.json build/packs/kompendium.report.json \
  || { echo "docs describe a different pack: tools/docs.py --snapshot build/packs/kompendium.report.json" >&2; exit 1; }

step "portable core: sanitizers, fuzz, differential, line coverage"
tests/native/run.sh

step "native lint"
(cd app && ufbt -s lint)

step "native build"
while IFS=$'\t' read -r id _version status; do
  if [ "$status" = official ] || python3 tools/sdk.py check "$id" >/dev/null 2>&1; then
    tools/build.sh "$id"
  else
    echo "skip $id: SDK not installed"
  fi
done < <(python3 tools/sdk.py list)

if [ "${VERIFY_SKIP_GOVERNANCE:-0}" != 1 ]; then
  step "governance"
  majordomus doctor | tail -n 1
fi
echo
echo "verify: PASS"
