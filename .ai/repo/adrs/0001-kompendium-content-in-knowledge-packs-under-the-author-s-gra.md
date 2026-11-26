---
schema: adr/v1
id: adr-0001
kind: adr
title: Kompendium content in knowledge packs under the author's grant
status: accepted
date: 2026-11-25
tags:
  - licensing
  - knowledge
provenance:
  origin: authored
---

# 1. Kompendium content in knowledge packs under the author's grant

## Context

Prismatic Pocket compiles knowledge objects (patterns, fallacies, signatures, protocols) into
`.pkp` packs. The only public, structured source with that content is
`korczis/kompendium-epistemicke-anihilace` (inspected at `2d08d1bf6ab4`). Since 2026-11-10 its
content is under the General Honest License v1.0, which forbids copying, derivative works and
incorporation into other software without a License Grant from the author. This repository's
code is MIT. The private `prismatic-platform` holds an older, terser copy of the same content,
so it adds nothing and stays out of this public repository.

The owner was asked in session on 2026-11-25 (about 03:06 UTC), after the inventory surfaced the
licence conflict. The question, as surfaced: copying GHL content into this MIT repository
needs an explicit owner decision (licence grant or relicensing). The owner's answer, verbatim:

> jsem autor, takze schvaluji prenos ;-)

("I am the author, so I approve the transfer.")

## Decision

Content derived from the Kompendium may be included in this repository's knowledge sources
and compiled packs under the author's grant above. Every derived object carries provenance
(source repository, commit, path). Derived knowledge content is not relicensed: it stays
the Kompendium author's work, included here by grant, and `knowledge/LICENSE.md` says so. The
code (runtime, compiler, tools) stays MIT.

## Alternatives rejected

- MIT paraphrase written from scratch: loses provenance to the canonical text and duplicates
  the catalogue by hand.
- Content from `prismatic-platform`: private, and older than the public Kompendium.
- Relicensing the derived content to MIT: the owner approved the transfer, not a relicence.
  This record does not assume one.

## Consequences

- `knowledge/LICENSE.md` names the grant and points here; the README's licence section
  separates code (MIT) from knowledge content.
- The compiler refuses an object without provenance.
- If the owner later relicenses the content, a superseding decision records it.

## Acceptance

Accepted by the owner on 2026-11-26, answering "Prijmout vsech 5 (Recommended)" to the question whether to accept ADR 0001 to 0005 (session f853febc-92e5-442d-804a-6721ed54bca7; decision "Accept ADR 0001 to 0005").
