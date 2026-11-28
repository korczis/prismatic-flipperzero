# Prismatic Flipper Zero: developer operations. `just` lists them.
# Every recipe is a thin call into tools/; the scripts are the documented interface.

set shell := ["bash", "-euo", "pipefail", "-c"]

# List recipes
default:
    @just --list --unsorted

# Readiness: host toolchain, SDKs, GitHub, device, Majordomus (device absent = BLOCKED, not failure)
doctor:
    tools/doctor.sh

# Install a pinned SDK from tools/sdks.json (official | momentum), checksum-verified
sdk id="official":
    python3 tools/sdk.py install {{id}}

# Build the FAP against one pinned SDK into build/<sdk>/ with build.json provenance
build sdk="official":
    tools/build.sh {{sdk}}

# Build for every SDK in the matrix
build-all:
    python3 tools/sdk.py list | cut -f1 | while read -r id; do tools/build.sh "$id"; done

# Regenerate derived sources (app/version.h from VERSION, icons from tools/icons.py)
gen:
    python3 tools/version.py
    python3 tools/icons.py

# Extract the pinned Kompendium sources and compile the knowledge pack (build/packs/)
pack:
    python3 tools/pocket.py build

# Copy the knowledge pack to the Flipper's SD card
deploy-pack:
    tools/device.sh deploy-pack

# Native lint (clang-format check via uFBT)
lint:
    cd app && ufbt -s lint

# Apply the SDK's clang-format style to app/
format:
    cd app && ufbt -s format

# Tool tests with the line-coverage gate and its self-test
test:
    python3 tests/tools/coverage_gate.py
    tests/tools/coverage_gate_selftest.sh

# Portable C core: sanitizers, mutation fuzz, C-vs-Python differential, llvm-cov line gate
test-native:
    tests/native/run.sh

# Build, install and start the app on the connected Flipper
launch sdk="official":
    tools/device.sh launch {{sdk}}

# Install the built FAP on the Flipper without starting it
deploy sdk="official": (build sdk)
    tools/device.sh deploy {{sdk}}

# Save one real frame of the Flipper screen
screenshot out="build/screen.png":
    tools/device.sh screenshot {{out}}

# Press keys on the device and save a real frame after each (keys: up,down,left,right,ok,back)
walk dir keys settle="0.4":
    tools/device.sh walk {{dir}} {{keys}} --settle {{settle}}

# Host bridge: status, push-pack, pull over RPC; sessions, progress decode pulled files
bridge cmd *args:
    @if [ "{{cmd}}" = sessions ] || [ "{{cmd}}" = progress ]; then python3 tools/bridge.py {{cmd}} {{args}}; else tools/device.sh bridge {{cmd}} {{args}}; fi

# The one verification entry point (CI and majordomus finish use it too)
verify:
    tools/verify.sh

# Assemble release assets (FAPs per SDK, SHA256SUMS, build provenance) into build/release/
release-assets:
    tools/release.sh

# Majordomus release record of published tags (.ai/repo/releases), read back from GitHub
release-record *tags:
    tools/release_record.py {{tags}}

# Pages explorer: data from the pack, site build, e2e in Chromium, JS line coverage
site:
    python3 tools/explorer.py
    cd site && yarn install --frozen-lockfile && node build.mjs && npx playwright test && node coverage.mjs

# Serve the built explorer locally
site-serve port="4790":
    python3 -m http.server {{port}} --bind 127.0.0.1 --directory site/dist

# Regenerate the GitHub wiki from README and docs, and push it
wiki:
    rm -rf build/wiki && git clone -q git@github.com:korczis/prismatic-flipperzero.wiki.git build/wiki
    python3 tools/wiki.py build/wiki
    cd build/wiki && git add -A && (git diff --cached --quiet || git commit -q -m "docs(wiki): regenerate from $(git -C ../.. rev-parse --short HEAD)") && git push -q origin HEAD

# The UI on the host: firmware fonts, every scenario, sanitizers, 100 % line coverage of app/ui
test-ui:
    tests/sim/run.sh

# Run the app on the host with a key script; frames land in build/sim/run-* as .bin
sim keys pack="build/packs/kompendium.pkp":
    tests/sim/build.sh && d="$(mktemp -d build/sim/run.XXXX)" && build/sim/pocketsim {{pack}} "$d" "{{keys}}" && echo "$d"

# Device and simulator draw the same pixels for the same keys (app running on the Flipper)
parity *args:
    tools/parity.sh {{args}}

# The acceptance test: add one object, recompile only the pack, the FAP's sha256 unchanged;
# --device repeats it on the Flipper
acceptance *args:
    tools/acceptance.sh {{args}}

# Render every screen and view into docs/evidence/atlas (CI checks it is current)
atlas:
    tools/atlas.py

# Measure the read cache: the device tour with and without it (docs/evidence/cache)
cache-bench:
    tools/cache-bench.sh
