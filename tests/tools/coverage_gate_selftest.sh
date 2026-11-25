#!/usr/bin/env bash
# Proves the coverage gate refuses: a copy of tools/ with one untested function must fail it.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/tests"
cp -R "$root/tools" "$tmp/tools"
cp -R "$root/tests/tools" "$tmp/tests/tools"
cp -R "$root/app" "$tmp/app"
cp "$root/VERSION" "$tmp/VERSION"
printf '\n\ndef untested_probe():\n    return 42\n' >> "$tmp/tools/version.py"
if python3 "$tmp/tests/tools/coverage_gate.py" >"$tmp/out" 2>&1; then
  echo "coverage selftest: gate PASSED a tree with an untested line" >&2
  exit 1
fi
grep -q 'tools/version.py:' "$tmp/out" || { cat "$tmp/out" >&2; exit 1; }
echo "coverage selftest: gate refused the untested line, as it must"
