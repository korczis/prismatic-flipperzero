# Development

## Prerequisites

`just doctor` reports each concern on its own line: host toolchain, SDKs, GitHub, device and
Majordomus. A missing Flipper is `BLOCKED`, not a build failure.

- **uFBT 0.2.6** and the pinned SDKs from `tools/sdks.json` (`just sdk`, `just sdk momentum`).
  Each SDK lives in its own `UFBT_HOME`.
- **Python 3.12** for the compiler and tools (standard library only).
- **The Kompendium source**: a sibling checkout `../kompendium-epistemicke-anihilace`, or set
  `KOMPENDIUM_DIR`. The compiler reads the commit pinned in `knowledge/sources.json`, never the
  working tree.
- **clang + llvm** for the native tests (Xcode's toolchain on macOS, `apt install llvm clang` on
  Linux).

## The loop

```bash
just pack            # ~1.5 min: extract + compile; build/packs/kompendium.{pkp,report.json}
just test            # tool tests + line-coverage gate
just test-native     # portable core: sanitizers, fuzz, differential, llvm-cov gate
just launch          # build the FAP, install over the storage CLI, start with the loader
just walk build/w ok,down,ok 1.2   # drive the device and capture a frame after each key
just site            # Pages explorer: data, build, Playwright e2e, JS line coverage
tools/docs.py        # regenerate derived doc regions (CI fails if you forget)
just verify          # all of the above, the way CI runs it
```

## Rules that bind every change

- **Tests run every new line.** `tests/tools/coverage_gate.py` covers `tools/*.py` and
  `tests/native/run.sh` covers `app/core/*.c`. Each refuses an untested line, and each has a
  self-test.
- **Derived, not typed.** Numbers, galleries and timelines in Markdown live between
  `<!-- docs:begin … -->` markers and come from `tools/docs.py`; `--check` runs in verify and CI.
- **UI is Flowbite on Tailwind 3, charts are ECharts through one layer.** `site/src/viz.js` is
  the only place that creates a chart; every chart is in its registry, maximizable, with its
  state in the URL. `site/coverage.mjs` fails on any line of `app.js` or `viz.js` that no
  Playwright test ran.
- **Hardware claims need frames.** A feature that runs on the device is shown with frames from
  `just walk` or `just screenshot`.
- **Governance.** Work runs as Majordomus tasks (`majordomus start … --scope …`), with
  `majordomus check` before claiming progress and `finish --verify-command "tools/verify.sh"` to
  close.

## Adding knowledge

Add or change pages in the Kompendium, move the pinned commit in `knowledge/sources.json`, and run
`just pack`. If a new collection or heading needs a role, change the map in `sources.json`. The
FAP does not change.
