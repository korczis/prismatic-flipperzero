#!/usr/bin/env bash
# The UI's host test: every scenario in tests/sim/scenarios.txt runs the real app (app/ui) on
# hermetic packs under ASan/UBSan with coverage; each must show its expected text, and every
# line with code in app/ui/*.c must have run (llvm-cov), or this fails naming what is missing.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
out="$root/build/sim/cov"
mkdir -p "$out"
find "$out" -name '*.profraw' -delete
python3 tests/sim/packs.py build/sim/packs >/dev/null
tests/sim/fetch.sh
python3 tools/fonts.py --check # app/ui/fonts.c is what the pinned u8g2 gives
if [ "$(uname)" = Darwin ]; then
  cc=(clang); cov=(xcrun llvm-cov); prof=(xcrun llvm-profdata)
else
  v="$(find /usr/bin -maxdepth 1 -name 'llvm-cov*' 2>/dev/null | sort -V | tail -n 1)"
  cc=("${CLANG:-clang}"); cov=("${LLVM_COV:-${v:-llvm-cov}}")
  prof=("${LLVM_PROFDATA:-${v/llvm-cov/llvm-profdata}}")
fi
# GCC warns where clang does not (format truncation); CI's cc is GCC, so build with one here too
for g in gcc-16 gcc-15 gcc-14 gcc-13 gcc; do
  if command -v "$g" >/dev/null 2>&1 && "$g" --version 2>/dev/null | grep -q GCC; then
    CC="$g" tests/sim/build.sh >/dev/null || { echo "ui: the GCC build ($g) failed" >&2; exit 1; }
    break
  fi
done
CC="${cc[*]}" tests/sim/build.sh -O0 -fprofile-instr-generate -fcoverage-mapping \
  -fsanitize=address,undefined -fno-sanitize-recover=all
trim() { local s="$1"; s="${s#"${s%%[![:space:]]*}"}"; printf '%s' "${s%"${s##*[![:space:]]}"}"; }
fail=0 n=0
while IFS='|' read -r name pack keys expect; do
  name="$(trim "$name")"
  [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue
  pack="$(trim "$pack")" keys="$(trim "$keys")" expect="$(trim "$expect")"
  case "$pack" in
    -) arg="-" ;;
    !*) arg="!build/sim/packs/${pack:1}.pkp" ;;
    *) arg="build/sim/packs/$pack.pkp" ;;
  esac
  dir="$out/frames/$name"
  rm -rf "$dir" # each scenario starts with no frames and no saved quiz progress
  mkdir -p "$dir"
  n=$((n + 1))
  if ! LLVM_PROFILE_FILE="$out/$name-%p.profraw" build/sim/pocketsim "$arg" "$dir" "$keys" \
    >"$dir/out.txt" 2>"$dir/err.txt"; then
    echo "FAIL $name: simulator exited non-zero"; cat "$dir/err.txt"; fail=1; continue
  fi
  IFS=';' read -ra wants <<<"$expect"
  for w in "${wants[@]}"; do
    w="$(trim "$w")"
    if [ "${w:0:1}" = "!" ]; then
      grep -qF -- "${w:1}" "$dir/out.txt" && { echo "FAIL $name: shows '${w:1}'"; fail=1; }
    else
      grep -qF -- "$w" "$dir/out.txt" || { echo "FAIL $name: missing '$w'"; fail=1; }
    fi
  done
done <tests/sim/scenarios.txt
[ "$n" -gt 0 ] || { echo "no scenarios ran" >&2; exit 1; }
echo "ui scenarios: $n ran"
"${prof[@]}" merge -sparse "$out"/*.profraw -o "$out/ui.profdata"
"${cov[@]}" export -format=lcov -instr-profile "$out/ui.profdata" build/sim/pocketsim \
  >"$out/coverage.lcov"
python3 tests/native/coverage_check.py "$out/coverage.lcov" app/ui || fail=1
# every screen and view has a current frame in the atlas (docs/evidence/atlas)
python3 tools/atlas.py --check || fail=1
if [ "$fail" -ne 0 ]; then
  echo "ui: FAIL" >&2
  exit 1
fi
echo "ui: PASS"
