---
schema: adr/v1
id: adr-0004
kind: adr
title: Prismatic crisis simulations ship in the public pack, imported as data at the owner's request
status: proposed
date: 2026-11-25
tags:
  - knowledge
  - provenance
provenance:
  origin: authored
---

# 4. Prismatic crisis simulations ship in the public pack, imported as data at the owner's request

## Context

The Prismatic Platform's `PrismaticCrisis.ScenarioEngine`
(`apps/prismatic/lib/prismatic_crisis/scenario_engine.ex`, GHL v2.0, by the same author) defines
nine tabletop exercises as literal Elixir maps: a briefing, objectives, rules and steps of options
with consequences and trust changes, plus the rule that decides a run's outcome. On 2026-11-25
the owner asked, verbatim:

> pokracu rozsir o vse z ~/dev/prismatic-platform o simulatory, interaktivni vyhodnocovace, vse
> data driven, ai powered, ..

("continue, extend with everything from ~/dev/prismatic-platform: simulators, interactive
evaluators, everything data-driven, AI-powered, ...").

## Decision

`tools/prismatic_import.py` reads the module at commit `49adc18f7bac` with `git show` and parses
its literals as data (nothing is executed) into `knowledge/scenarios.json`, which is committed
because the Prismatic Platform is private and CI cannot read it. The compiler turns each scenario
into a knowledge object with the capability `simulate`; the outcome rule is ported to C
(`app/core/simulate.c`) with the source's thresholds. Every run carries a note that it is a
training exercise, not guidance for a real emergency. "AI-powered" is not taken to mean a model
on the device: there is none; the device runs the platform's own data and rules offline.

## Consequences

- `tools/verify.sh` runs `tools/prismatic_import.py --check`, which fails when the committed JSON
  differs from a fresh import of the pinned commit (and says it skipped when the source is absent).
- Moving the pin re-imports the scenarios in one command; a scenario added to the platform reaches
  the device as data, with no new code.
