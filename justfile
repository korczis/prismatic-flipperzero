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

# The one verification entry point (CI and majordomus finish use it too)
verify:
    tools/verify.sh

# Assemble release assets (FAPs per SDK, SHA256SUMS, build provenance) into build/release/
release-assets:
    tools/release.sh
