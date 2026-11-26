---
schema: adr/v1
id: adr-0005
kind: adr
title: Host bridge is a host CLI over the Flipper's own RPC, by owner choice
status: accepted
date: 2026-11-26
tags:
  - bridge
  - hardware
provenance:
  origin: authored
---

# 5. Host bridge is a host CLI over the Flipper's own RPC, by owner choice

## Context

The first brief of 2026-11-25 was "a Flipper app with a host bridge". Task
t-20261125024351-df56 ("Protocol spec and native Flipper app", scope protocol, bridge, web) was
reprioritised by the owner to First Light and never resumed; no specification exists. v0.1.0
shipped without a bridge. On 2026-11-26 the owner was asked (AskUserQuestion, session
f853febc-92e5-442d-804a-6721ed54bca7):

> Nedokoncena nit z handoveru je host bridge (ukol 'Protocol spec and native Flipper app',
> nikdy nedokonceny, zadna spec v repu). Jakou podobu ma mit?

with four options: a host CLI over RPC, a live protocol from the FAP over USB CDC, a Prismatic
adapter (platform import plus the CLI as transport), or parking it. The owner answered, verbatim:

> Host CLI pres RPC (Recommended)

## Decision

The bridge is `tools/bridge.py`, a host program that speaks the Flipper's own RPC protocol
(flipperzero-protobuf: storage, system and app lock status) over USB. It reports the device and
the installed pack, installs a pack with a checksum check, copies the reader's saved sessions and
quiz progress off the card, and decodes them against the pack into JSON a person or another
program can read. The FAP gains no interface: its contract with the host stays the files under
`SD/apps_data/prismatic/`, which `docs/bridge.md` specifies.

## Alternatives rejected

- A live protocol from the FAP over USB CDC: a new input surface in the app, its own framing
  and fuzzing, for no capability the files do not already give.
- A Prismatic adapter on the platform side: the platform is private and a separate contract; the
  decoded JSON is the input such an adapter would take, so it can follow without changing this.
- Parking the thread: it was the first brief and the only open objective in the handovers.

## Consequences

- The bridge writes only `*.pkp` files under `/ext/apps_data/prismatic/`, through a temporary
  name and an MD5 check, and refuses while an application holds the device lock; it never deletes
  sessions or progress and never flashes firmware (`project.hardware-evidence`).
- Saved sessions and progress are the reader's own data: the bridge copies them only when asked
  and the repository records counts and hashes from a device run, never their content.
- Without a device, the bridge is tested against a fake RPC peer; with one, its run is recorded
  under `docs/evidence/bridge/`.

## Acceptance

Accepted by the owner on 2026-11-26, answering "Prijmout vsech 5 (Recommended)" to the question whether to accept ADR 0001 to 0005 (session f853febc-92e5-442d-804a-6721ed54bca7; decision "Accept ADR 0001 to 0005").
