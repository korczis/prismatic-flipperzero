---
schema: context/v1
id: ai.repo.releases
kind: context
title: Published releases
description: One record per published release, read back from GitHub after CI published it.
status: active
scope: subtree
providers: ["*"]
audience: [human, agent]
composition: extend
order: 75
---

# Published releases

One file per release tag, `v<major>.<minor>.<patch>.yaml`, contract `release/v1`. A record
says which tag was published, which commit it was built from, its channel (every release so
far is a preview, so `prerelease`), and for every asset its name, URL, SHA-256 and size.

## Who writes these

`just release-record <tag>` (`tools/release_record.py`), once the tag's CI run has published
the release and re-verified its downloads. It reads the release back from GitHub and refuses
to write a record unless every digest GitHub reports matches the release's own `SHA256SUMS`.
Nobody edits a record by hand: it is evidence of what was published, not a plan. The record
lands on `main` as a `chore(release)` commit, which is how Majordomus measures whether landed
work is released (`majordomus run release.currency`).
