# Knowledge content licence

The knowledge packs built from `knowledge/sources.json` contain text derived from the
[Kompendium epistemické anihilace](https://github.com/korczis/kompendium-epistemicke-anihilace)
(licensed GHL v1.0 by its author). It is included here by the author's grant, recorded with the
owner's verbatim answer in `.ai/repo/adrs/0001-kompendium-content-in-knowledge-packs-under-the-author-s-gra.md`;
the catalogue trainer examples are included by the decision in
`.ai/repo/adrs/0002-trainer-utterances-ship-in-the-public-pack-by-owner-bypass-o.md`.

The assessments in `knowledge/applications.toml` (the Admiralty code, the due-diligence evidence
levels and the OSINT confidence levels) come from the Prismatic Platform (GHL v2.0, by the same
author), copied at the owner's request recorded in
`.ai/repo/adrs/0003-prismatic-assessment-scales-ship-as-applications-by-owner-request.md`;
each names its source path and commit. The crisis simulations in `knowledge/scenarios.json` are
imported from the same platform by `tools/prismatic_import.py` at the owner's request recorded in
`.ai/repo/adrs/0004-prismatic-crisis-simulations-ship-as-simulations-by-owner-request.md`.

The derived content is not relicensed. The code in this repository (runtime, compiler, tools) is
MIT; see `/LICENSE`. Every object in a pack records its source repository, commit and path.
