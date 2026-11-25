---
schema: adr/v1
id: adr-0002
kind: adr
title: Trainer utterances ship in the public pack by owner bypass of Kompendium ADR 0008
status: proposed
date: 2026-11-25
tags:
  - knowledge
  - safety
provenance:
  origin: authored
---

# 2. Trainer utterances ship in the public pack by owner bypass of Kompendium ADR 0008

## Context

The Kompendium's fallacy and NLP catalogues (`data/fauly/*.toml`, `data/nlp/*.toml`) carry
trainer items: an example utterance, its context, whether it is manipulative or benign, the
marker and the defensive response. The Kompendium's own ADR 0008 (accepted 2026-11-11) rules out
any public surface that takes text in and returns matching entries, and its catalogue rule
states that trainer utterances are validated but published nowhere.

An independent review (Codex, 2026-11-25) flagged that a public pack would publish them. The
session told the owner it would leave them out. The owner answered on 2026-11-25 (about
03:35 UTC), verbatim:

> proto je do veřejného packu ani na Pages nedám <--- nikoliv, je to muj projekt, takze zde
> schvaluji bypass

("'so I will not put them in the public pack or on Pages' <--- no, it is my project, so I
approve the bypass here.")

## Decision

Trainer items are extracted into the IR and the `.pkp` pack and may appear on GitHub Pages, as
quiz questions of the form "manipulative or benign?" with the marker and the defensive
response as the explanation. Each carries provenance and the source's review status
(`review`).

What is not built: no surface where a reader types free text and receives matching entries or a
classification. ADR 0008's core concern stays respected, and only the publication of the
utterances is bypassed.

## Alternatives rejected

- Keep trainer items out of public artifacts (ADR 0008 as written): overridden by the owner.
- A local-only pack variant: unnecessary after the owner's answer.

## Consequences

- The compiler includes trainer items and reports their count per pack.
- The README and the pack licence note say the examples come from the Kompendium catalogues,
  published here by the author's decision recorded in this ADR.
- A free-text recognise feature would need its own decision.
