# Changelog

## Unreleased

- Host bridge, `tools/bridge.py` (`just bridge`): the Flipper's own RPC over USB. Device and
  card status, a pack installed through a temporary name with an MD5 check, saved sessions and
  quiz progress pulled and decoded against the pack; the FAP gains no interface (ADR 0005,
  `docs/bridge.md`). Proven on the device: status, a 20 MB install, pull, and decoding of a
  session saved through the detector. It never ends a session or releases the port while the
  device is still working, which is what stopped the Flipper four times during development.

## 0.1.0 — Pocket (preview)

- The full Kompendium (2 241 objects, 66 chapters, 11 kinds, 37 567 relations) compiled by
  `tools/pocket.py` into a deterministic, read-only `.pkp` pack; a new object changes only the
  pack, never the FAP (acceptance test, also on the device).
- Portable pack reader with bounds-checked records, an LRU block cache, ASan/UBSan, mutation
  fuzzing, a C-vs-Python differential test and a line-coverage gate.
- Interface of four hubs on a tile grid (Library, Detect, Act, Train), structured reader with
  contents outline, map of links, context menu and side-by-side confusables.
- Detector with saved sessions (JSON Lines, only when asked); assessments (Admiralty code, DD
  evidence levels, OSINT confidence), scorecards and the Bayesian fusion calculator from the
  Prismatic Platform, all live on the device.
- Training: term and manipulative-or-benign quizzes, spaced repetition in Leitner boxes with a
  torn-write-safe progress file, nine crisis simulations.
- Host simulator with the firmware's fonts, a screen atlas of every screen, device parity frame
  for frame; Pages explorer over the same pack; wiki rendered from README and docs.
- Release ships `kompendium-<version>.pkp` with its build report next to the FAPs.

## 0.0.1 — First Light (preview)

- Native dialog FAP: prism mark, name, milestone, version; firmware version read at runtime.
- Pinned, checksum-verified SDK matrix: official 1.5.1 (API 88.2), Momentum mntm-012 (API 87.1).
- Device tooling behind a lock: launch, deploy, real RPC screenshots and key walks.
- `just` recipes, a single `tools/verify.sh`, a line-coverage gate over `tools/*.py`.
- CI: tools, FAP matrix, Majordomus governance, prerelease with verified checksums.
