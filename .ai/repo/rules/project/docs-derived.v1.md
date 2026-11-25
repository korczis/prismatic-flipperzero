---
id: project.docs-derived
version: 1
kind: rule
title: Docs move with every commit, and their facts are derived
description: README, docs and the gallery are updated in the same change as the code they describe, and every number, gallery and timeline in them is generated and checked.
statement: Update README.md, docs/*.md and the device gallery in the same commit as the change they describe; write numbers, galleries and timelines only inside tools/docs.py regions, never by hand.
status: active
class: blocking
depends_on: []
tags: [docs, derived, enforcement]
---

# Rationale

A README that describes last week's app is worse than none. The owner's order is that the
docs, the Pages site and the screenshots move with every commit and that nothing in them is
typed when data can give it.

# Required behaviour

- Numbers about the pack live in `<!-- docs:begin pack-stats -->` style regions, rendered by
  `tools/docs.py` from `docs/data/pack-stats.json`, which is the pack build report.
- The gallery comes from `docs/evidence/pocket/manifest.json`, written by
  `tools/capture-screens.sh` from real device frames.
- The case-study timeline comes from git history.
- A change to the app, the compiler or the pack updates the prose that describes it in the
  same commit.

# Enforcement

- `tools/docs.py --check` runs in the pre-commit hook, in `tools/verify.sh` and in CI (job
  *Tools*), and fails on any drifted region.
- CI job *Knowledge pack* fails when `docs/data/pack-stats.json` differs from the report of
  the pack it just built.
- Prose accuracy beyond derived regions is a review finding, not a machine check.
