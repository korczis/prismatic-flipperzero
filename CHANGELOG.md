# Changelog

## Unreleased

- **Find by name** (Library, first entry): type a title, alias or code a letter at a time on a
  wheel that offers only letters with matches, so a query never reaches zero; the best three
  under the wheel, every match on OK, strong matches (the title's first word, the code) first.
  Prefix search on the device; the Pages explorer keeps its substring search.
- The pack carries a search index in two new optional sections (10 keys, 11 postings: 6 440
  keys, 17 220 postings, 137 480 bytes). The format version stays 1: the v0.2.0 app opens the
  new pack and offers no search, and this app opens a v0.2.0 pack without one (both proven in
  `tests/native/run.sh`). Sections 12-14 are reserved for a multi-pattern detector.
- `app/core/search.c` (1.2 KiB, no allocation), checked against the reference
  `tools/pocket.py search` on every prefix of one to three letters and every key typed in full;
  `tools/cache-bench.sh` measures what each Find step reads from the card in the simulator.
- Not yet on a device: the gallery, the parity record and the acceptance record still show the
  0.2.0 build.

## 0.2.0 — Bridge (preview)

- Host bridge, `tools/bridge.py` (`just bridge`), over the Flipper's own USB interfaces, with no
  interface added to the app (ADR 0005, `docs/bridge.md`): device and card status over RPC, a
  pack installed over the storage CLI in acknowledged chunks with MD5 checks on the device
  before the swap and after it, saved sessions and quiz progress pulled and decoded against the
  pack with every object, marker and question named.
- Proven on official 1.5.1: status, a 20 MB install (479 s), pull, and decoding of a session
  saved through the detector. The bridge never ends a session or releases the port while the
  device is still working, which is what stopped the Flipper four times during development.
- ADR 0001 to 0005 accepted; a UI workflow for the Pages explorer.

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
