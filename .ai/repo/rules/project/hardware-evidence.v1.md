---
id: project.hardware-evidence
version: 1
kind: rule
title: Hardware claims need hardware evidence
description: Install, launch and behaviour on the Flipper are claimed only with frames read from the device; firmware is never flashed implicitly.
statement: Claim device behaviour only with frames from the device's RPC screen stream and loader or CLI output; never present a mock-up as a screenshot and never flash firmware as a side effect of tooling.
status: active
class: blocking
depends_on: []
tags: [hardware, evidence]
---

# Required behaviour

- Screenshots come from `tools/flipper_screen.py` (RPC screen stream), never from a drawing.
- A build passing is not a launch, and a launch request is not visible behaviour: each is
  claimed with its own evidence (build output, loader output, frames).
- `tools/device.sh` installs FAPs and packs and drives the UI; it never flashes firmware.
- A missing device is reported as BLOCKED, not PASS.

# Enforcement

Review, plus the device tooling itself: no recipe in `justfile` or `tools/device.sh` writes
firmware.
