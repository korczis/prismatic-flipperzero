---
id: project.ui-atlas
version: 1
kind: rule
title: Every screen and view has a current frame in the screen atlas
description: tests/sim/atlas.txt walks to every screen and overlay of the app; docs/evidence/atlas is that walk rendered, checked byte for byte on every test run.
statement: A change that adds or alters a screen, view or overlay updates tests/sim/atlas.txt and re-renders docs/evidence/atlas with tools/atlas.py; the atlas must cover every Screen of app/ui/ui_app.c, each walk must end on the screen it declares, and the committed frames must equal a fresh render.
status: active
class: blocking
depends_on: [project.tests-cover-new-code@1, project.docs-derived@1, project.hardware-evidence@1]
tags: [ui, hci, evidence, screenshots]
---

# Required behaviour

- `tests/sim/atlas.txt` lists one walk per screen and view (`name | pack | keys |
  screen[+overlay] | caption`); a new `Screen` value in `app/ui/ui_app.c` gets a walk in the
  same change.
- `tools/atlas.py` renders the walks with the host simulator (the firmware's u8g2 and fonts, the
  same app code as the FAP) into `docs/evidence/atlas/`: one PNG per walk, `sheet.png` (all
  frames, for reviews) and `manifest.json` (captions, screens, keys), which the README and the
  docs render through `tools/docs.py`.
- A UI change is reviewed on the atlas, not on one screen: the contact sheet goes to an
  independent reviewer (another model or a peer) with the frame captions, and the review's
  findings are recorded in `docs/ui.md` with what was done about each.
- Atlas frames are simulator frames. A claim about the device itself still needs frames from the
  device (`project.hardware-evidence`, `tools/parity.sh`).

# Enforcement

`tests/sim/run.sh` (the `native` job in CI, `tools/verify.sh` locally) runs
`tools/atlas.py --check`, which fails when a walk ends elsewhere than it declares, when a Screen
has no frame, or when any file under `docs/evidence/atlas/` differs from a fresh render.
