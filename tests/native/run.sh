#!/usr/bin/env bash
# Native tests for the portable core (app/core), no device needed:
#   1. unit, corruption and mutation-fuzz tests under ASan + UBSan, warnings as errors
#   2. golden differential: the C dump of the full pack equals tools/pocket.py decode
#   3. differential fuzz: C and Python accept and reject the same mutated packs
#   4. line coverage: every executable line of app/core/*.c ran (llvm-cov), else fail
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
out=build/native
mkdir -p "$out"
cc="${CC:-cc}"
flags=(-std=c99 -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion -g)
src=(app/core/pkp.c app/core/pocket.c app/core/cache.c app/core/progress.c app/core/session.c app/core/simulate.c app/core/scorecard.c tests/native/test_pkp.c)

[ -f build/packs/kompendium.ir.json ] || python3 tools/pocket.py extract >/dev/null
python3 tools/pocket.py compile --out "$out/full.pkp" --report "$out/full.report.json" >/dev/null
python3 tools/pocket.py subset --prefix kompendium/techniques/ea-1- --out "$out/mini.ir.json" >/dev/null
python3 tools/pocket.py compile --ir "$out/mini.ir.json" --out "$out/mini.pkp" \
  --report "$out/mini.report.json" >/dev/null

echo "== sanitizers"
"$cc" "${flags[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -fno-sanitize-recover=all "${src[@]}" -o "$out/test_pkp"
"$out/test_pkp" test "$out/mini.pkp"

echo "== golden differential (full pack)"
"$out/test_pkp" dump "$out/full.pkp" > "$out/c-dump.json"
python3 tools/pocket.py decode "$out/full.pkp" > "$out/py-dump.json"
python3 -c 'import json,sys; a,b=(json.load(open(p)) for p in sys.argv[1:]); sys.exit(0 if a==b else "C dump differs from the Python reference")' \
  "$out/c-dump.json" "$out/py-dump.json"
echo "C dump == Python decode"

echo "== differential fuzz"
rm -rf "$out/diff"
python3 tests/native/differential.py "$out/test_pkp" "$out/mini.pkp" "$out/diff" 1500

echo "== line coverage of app/core"
if command -v xcrun >/dev/null 2>&1; then
  cov=(xcrun llvm-cov); prof=(xcrun llvm-profdata)
else
  v="$(find /usr/bin -maxdepth 1 -name 'llvm-cov*' 2>/dev/null | sort -V | tail -n 1)"
  cov=("${LLVM_COV:-${v:-llvm-cov}}"); prof=("${LLVM_PROFDATA:-${v/llvm-cov/llvm-profdata}}")
fi
"${CLANG:-clang}" "${flags[@]}" -O0 -fprofile-instr-generate -fcoverage-mapping "${src[@]}" \
  -o "$out/test_pkp_cov"
rm -f "$out"/*.profraw
LLVM_PROFILE_FILE="$out/t-%p.profraw" "$out/test_pkp_cov" test "$out/mini.pkp" >/dev/null
LLVM_PROFILE_FILE="$out/d-%p.profraw" "$out/test_pkp_cov" dump "$out/full.pkp" >/dev/null
"${prof[@]}" merge -sparse "$out"/*.profraw -o "$out/core.profdata"
"${cov[@]}" export -format=lcov -instr-profile "$out/core.profdata" "$out/test_pkp_cov" \
  > "$out/coverage.lcov"
python3 tests/native/coverage_check.py "$out/coverage.lcov"
echo
echo "native: PASS"
