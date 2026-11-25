---
schema: knowledge/v1
id: majordomus-adopter-findings-2026-11-25
kind: knowledge
class: lesson
title: "Majordomus features this adopter had to hand-build, mapped to existing records and new intake candidates"
description: "Read-only mapping against prismatic-majordomus of what building this repository needed: derived regions, wiki projection, scope widening, gate negative controls, fleet-pack rules, codex-exec stdin."
status: verified
epistemics: observed
date: 2026-11-25
tags:
  - majordomus
  - adopter
about: "tools/docs.py"
route: project
provenance:
  origin: extracted
  derived_from:
    - file:tools/docs.py
    - file:tools/wiki.py
    - file:tests/tools/coverage_gate_selftest.sh
---
# Majordomus adopter findings, 2026-11-25

Building this repository showed several features that every adopter currently builds by
hand. A read-only mapping against prismatic-majordomus (checkout `ae13be1bd13`; key files equal
to `origin/master` `46775282570`) found the following. No ids were allocated. The worker that
collects adopter data points was not reachable from this session, so this file is the record
until intake takes it.

## Data points for existing records

- **I4263 and I4267** (derived regions, site as a deployment). This repository hand-built
  `tools/docs.py`, which fills `<!-- docs:begin NAME -->` regions from data files, from git
  history and from C `#define`s, with `--check` run in pre-commit, verify and CI. It also
  hand-built `tools/wiki.py` (wiki pages rendered from the repository's Markdown) and a Pages
  workflow. Proposal: a region generator may be a repository command or data file. Task
  `t-20261125064128-ba3f` (I4752, generated regions) overlaps this.
- **I4260** (`scope add`). This is the third adopter refused at pre-push for an intended change
  to a tracked hook file outside the task scope. The only remedy was `handover --close` plus a
  new `start`.
- **Drift.** I2114 (`lib/check.sh:203`) and I4598 (`lib/check.sh:249`) are on master but still
  READY in the plan.

## New candidates

1. **History-derived regions.** A region derived from history is current when it is a prefix
   of the history, because a commit cannot list itself. See `tools/docs.py` `current()`.
2. **Commit-time scope check.** Promote knowledge `s-20261111110145-5143-b43cfeca0cf7` to an
   issue (neighbour I4573).
3. **Gate negative-control harness.** Prove that a gate refuses a planted violation, and that
   its isolated copy holds everything its tests read. Here the coverage self-test failed for
   the wrong reason, because tests read README.md, which the copy lacked.
4. **Fleet-pack rules**, enforced through I4255:
   - tests cover new code, with a coverage gate per language;
   - docs are derived and checked;
   - claims need evidence artifacts.

   This repository's versions are in `.ai/repo/rules/project/`.
5. **codex-exec advisor.** `scripts/lib/advisors/codex-exec.mjs:23` passes the prompt in argv.
   It should go on stdin (`-`), because `-i <file>...` is variadic and swallows a prompt that
   follows it. This session hit exactly that.

## Positive evidence

- `doctor` refused a commit whose rewritten README no longer named AGENTS.md (I2941). This
  worked as intended.
