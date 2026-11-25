---
id: project.tests-cover-new-code
version: 1
kind: rule
title: Every changed line is run by a test
description: Tools are gated by sys.monitoring line coverage, the portable C core by llvm-cov, and device code by real device frames.
statement: Ship tests that run every line a change adds or modifies; the coverage gates refuse any line no test ran.
status: active
class: blocking
depends_on: []
tags: [tests, coverage, enforcement]
---

# Required behaviour

- `tools/*.py`: `tests/tools/coverage_gate.py` fails on any executable line no test ran;
  `tests/tools/coverage_gate_selftest.sh` proves the gate refuses an untested line.
- `app/core/*.c`: `tests/native/run.sh` runs the core under ASan and UBSan, mutation-fuzzes it,
  compares it with the Python reference and fails on any line with code that never ran.
- Device code (`app/prismatic.c`, `app/reader.c`) cannot run on the host. It is verified with
  real frames from `just walk` or `tools/capture-screens.sh`, recorded under `docs/evidence/`.

# Enforcement

CI jobs *Tools* and *Portable core*, and `tools/verify.sh`, which `majordomus finish` runs.
