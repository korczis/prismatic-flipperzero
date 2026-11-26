---
schema: adr/v1
id: adr-0003
kind: adr
title: Prismatic assessment scales ship in the public pack as applications, at the owner's request
status: accepted
date: 2026-11-25
tags:
  - knowledge
  - provenance
provenance:
  origin: authored
---

# 3. Prismatic assessment scales ship in the public pack as applications, at the owner's request

## Context

The device had a library, detectors, a trainer and review, but no tool to *grade* something.
The Prismatic Platform (`gitlab.com/korczis/prismatic-platform`, licensed GHL v2.0, "all rights
reserved") defines several grading scales as data:

- the NATO Admiralty System (source reliability A-F, information credibility 1-6) with grading
  rules, in `apps/prismatic/priv/prompt_library_seeds/osint-assess-source-reliability.json`;
- the due-diligence evidence levels 0-9 in `apps/prismatic_dd/lib/prismatic_dd/dd_level.ex`;
- the OSINT confidence levels in
  `apps/prismatic/lib/prismatic_patterns/adapters/osint_domain_adapter.ex`.

On 2026-11-25 the owner, who authors both the Prismatic Platform and this project, asked
verbatim:

> dotahni vsechno, pridej celou databazi nlp patterns, pridej poradny readme (inspired by fael -
> interaktivni), udelej cele ui interaktivni, priddej ruzne aplikace (rozumej hodnoceni,
> prismatic veci, etc)

("finish everything, add the whole NLP patterns database, add a proper README (inspired by
FAEL, interactive), make the whole UI interactive, add various applications (meaning
assessments, Prismatic things, etc)").

## Decision

The three scales ship in the public pack as knowledge objects with the new capability `assess`
(`knowledge/applications.toml`, category "Applications"). Only their labels, the level meanings
the sources give, and the sources' grading rules are copied, each with its source path and the
Prismatic Platform commit (`49adc18f7bac`). A meaning the source does not give stays empty; no
grade is computed by the device, the reader chooses it. The prompt text, its JSON output
contract and every other part of the Prismatic Platform stay where they are.

Later the same day, at the owner's request to extend with "everything from
~/dev/prismatic-platform: simulators, interactive evaluators" (verbatim in ADR 0004), two more
scales (estimative language, threat-indicator age) and two scorecards (the executive frame of
`apps/prismatic_dd/lib/prismatic_dd/executive_frame.ex` and the nominee-shareholder check of
`apps/prismatic_cer/lib/prismatic_cer/nominee_risk.ex`) joined them under the same terms. A
scorecard copies the source's base, weights, clamp, bands and red-flag texts.

## Consequences

- Adding another scale is a data change in `knowledge/applications.toml`; the device walks any
  number of scales (up to four per assessment, sixteen grades each) without new code.
- A scale whose source changes is updated by hand from the cited path and commit; there is no
  extractor for these files yet, because their formats (an Elixir map, a JSON prompt) differ.

## Acceptance

Accepted by the owner on 2026-11-26, answering "Prijmout vsech 5 (Recommended)" to the question whether to accept ADR 0001 to 0005 (session f853febc-92e5-442d-804a-6721ed54bca7; decision "Accept ADR 0001 to 0005").
