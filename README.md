<p align="center">
  <img src="docs/assets/hero.svg" alt="Prismatic Pocket: the Kompendium split into detect, inspect, understand, apply, verify and learn" width="100%">
</p>

<p align="center">
  <a href="https://github.com/korczis/prismatic-flipperzero/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/korczis/prismatic-flipperzero/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/korczis/prismatic-flipperzero/releases"><img alt="Release" src="https://img.shields.io/github/v/release/korczis/prismatic-flipperzero?include_prereleases&label=release&color=ff8a00"></a>
  <img alt="Firmware" src="https://img.shields.io/badge/firmware-official%201.5.1%20%7C%20Momentum%20mntm--012-ff8a00">
  <a href="https://korczis.github.io/prismatic-flipperzero/"><img alt="Explorer" src="https://img.shields.io/badge/explore-online-38d96b"></a>
  <a href="docs/case-study.md"><img alt="Case study" src="https://img.shields.io/badge/case%20study-zero%20to%20app-a24dff"></a>
  <a href="https://majordomus.dev"><img alt="Supervised by Majordomus" src="https://img.shields.io/badge/supervised%20by-Majordomus-4d5bff"></a>
</p>

<p align="center">
  <b>Prismatic Pocket</b> puts a whole book on epistemic self-defence into a Flipper Zero.<br>
  Read it, recognise what is happening to you, decide what to do, and train until it is reflex,<br>
  with no network, no phone and no account.
</p>

<p align="center">
  <a href="#what-it-is">What it is</a> ·
  <a href="#the-interface">Interface</a> ·
  <a href="#on-a-real-device">On a real device</a> ·
  <a href="#small-memory-by-design">Small memory</a> ·
  <a href="#what-is-inside">What is inside</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#install">Install</a> ·
  <a href="#build-it-yourself">Build</a> ·
  <a href="#how-it-is-kept-honest">Guarantees</a> ·
  <a href="docs/case-study.md">Case study</a> ·
  <a href="https://korczis.github.io/prismatic-flipperzero/">Explorer</a> ·
  <a href="https://github.com/korczis/prismatic-flipperzero/wiki">Wiki</a>
</p>

> **Try it without a Flipper.** The [online explorer](https://korczis.github.io/prismatic-flipperzero/)
> is the same pack in a browser: a drillable map of every category, search, every article with
> its relation tree, the detector and both quizzes. Object *n* there is object *n* on the device.
> [The device](https://korczis.github.io/prismatic-flipperzero/#/device) page shows every screen
> of the app full size, filterable by screen, with the keys that reach it.

---

## What it is

Manipulation rarely announces itself. It shows up as a deadline you cannot verify, a choice
narrowed to two bad options, a colleague who remembers the meeting differently every time, an
expert whose credentials quietly stop counting. The
[Kompendium epistemické anihilace](https://github.com/korczis/kompendium-epistemicke-anihilace)
is a book about exactly that. It describes how people's ability to know things is undermined and
how to defend it: techniques, detection signatures, fallacies, NLP patterns, case studies,
emergency and recovery protocols, a glossary.

A book is the wrong shape for the moment you need it. When the pressure is on you need three
things fast: *is this the pattern I think it is*, *what else could it be*, and *what do I do
now*. **Prismatic Pocket** turns the book into that tool and puts it in a device you already carry
for other reasons.

- **Encyclopedia.** Every page of the Kompendium becomes an article: a definition, the markers to
  look for, what to do, when to escalate, what not to confuse it with, the chapters, and links to
  everything related.
- **Detector.** Where the book lists observable markers, you walk through them one at a time
  and mark each one *observed*, *not observed*, *unknown* or *not applicable*. You get your marks
  back, then what to rule out first, how to validate it, and the next steps. The detector **does
  not score, rank or diagnose anyone**. A marker describes; it never proves.
- **Applicator.** Every article with response guidance offers it directly: first safe reaction,
  escalation and handover.
- **Trainer.** Two quiz modes derived from the data. *Which term is this?* masks the name in a
  definition and offers confusable alternatives. *Manipulative or benign?* uses the catalogue's
  example utterances and explains the marker and a good response.

Everything on the device is **derived** from the book by a compiler. There is no hand-written
content in the app and no per-topic code: adding a page to the Kompendium adds an article,
and its detector and quiz when the page carries the material for them.

## The interface

A 128 x 64 monochrome screen and five buttons hold two thousand articles. The design work that
made this usable is a question of **information architecture**: where each thing lives, how many
presses away it is, and what the screen says about where you are. The rules come from earlier
small-screen software (Palm OS, feature-phone browsers, the TI-83, the 1984 Macintosh), from an
independent review of every screen by another model, and from the device itself
([design notes](docs/ui.md)).

<p align="center">
  <img src="docs/evidence/atlas/sheet.png" width="100%" alt="Every screen of the app: a contact sheet of 31 frames from the screen atlas">
  <br><sub>Every screen and view on one sheet: the <a href="#every-screen">screen atlas</a>, rendered pixel-exact.</sub>
</p>

### Information architecture

Home is a grid of six tiles. Four are **hubs**, one per thing you came to do; each shows how
much is behind it, and a status line at the bottom explains the focused tile in one line. A hub
is a short menu, and every entry, count and share bar in it comes from the pack: an entry with
nothing behind it is not shown. The tree below is generated from the hubs table in
[`app/ui/ui_app.c`](app/ui/ui_app.c), so it cannot drift from the app.

<!-- docs:begin ia-tree -->
```mermaid
flowchart LR
  home(["Home"])
  home --> h0["Library"]
  h0 --> h0e0["By chapter<br/><small>Chapters in book order</small>"]
  h0 --> h0e1["By kind<br/><small>One list per kind</small>"]
  h0 --> h0e2["Everything<br/><small>All, in pack order</small>"]
  h0 --> h0e3["Surprise me<br/><small>A random article</small>"]
  home --> h1["Detect"]
  h1 --> h1e0["Detectors<br/><small>Objects with markers</small>"]
  h1 --> h1e1["Random detector<br/><small>Markers of a random one</small>"]
  home --> h2["Act"]
  h2 --> h2e0["What to do<br/><small>Objects with steps</small>"]
  h2 --> h2e1["Confusables<br/><small>Pairs to tell apart</small>"]
  home --> h3["Train"]
  h3 --> h3e0["Which term?<br/><small>Pick the term of four</small>"]
  h3 --> h3e1["Manipulative?<br/><small>Manipulative or benign</small>"]
  h3 --> h3e2["Review<br/><small>Due again: boxes 1 to 5</small>"]
  h3 --> h3e3["Quiz an object<br/><small>Quiz a chosen object</small>"]
  home --> t0["Info"]
  home --> t1["About"]
```
<!-- docs:end ia-tree -->

Every article is a hub of its own: chips for its detector and its map of links sit one press
below the title, holding OK opens its actions and contents, and **See also** and **Don't confuse
with** link onwards. Five ways in reach the same object: by chapter, by kind, a filtered list,
a link from another article, or the map.

| Key | Home grid | Lists and hubs | Articles | Anywhere |
|---|---|---|---|---|
| Up / Down | row above / below, then leave the grid | next row (wraps) | next link in view, else scroll a line | |
| Left / Right | tile beside, stops at the edge | four rows | previous / next section | |
| OK | open the hub | open the row | open the focused link or chip | |
| Hold OK | | context menu | actions and contents | |
| Back | quit | one level up, focus and scroll restored | one level up | |
| Hold Back | | Home | Home | Home |
| Rest | | a cut name scrolls into view after 2.5 s | | |

### Every screen

The screen atlas walks to every screen and overlay of the app and keeps its last frame.
`tests/sim/run.sh` re-renders it and fails when a screen has no frame, when a walk lands
somewhere else than it says, or when a committed frame differs from the fresh render.

<!-- docs:begin atlas -->
<table>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/01-home.png" width="384" alt="Home: six tiles with counts; the status line explains the focused one"><br><sub>Home: six tiles with counts; the status line explains the focused one</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/02-home-detect.png" width="384" alt="Right moves along a row of the grid; the hint follows the focus"><br><sub>Right moves along a row of the grid; the hint follows the focus</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/03-home-info.png" width="384" alt="The third row: Info and About, reached by scrolling whole rows"><br><sub>The third row: Info and About, reached by scrolling whole rows</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/04-hub-library.png" width="384" alt="Library: chapters, kinds, everything, a random article"><br><sub>Library: chapters, kinds, everything, a random article</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/05-hub-detect.png" width="384" alt="Detect: every detector, or a random one"><br><sub>Detect: every detector, or a random one</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/06-hub-act.png" width="384" alt="Act: what to do, and pairs to tell apart, with their share of the pack"><br><sub>Act: what to do, and pairs to tell apart, with their share of the pack</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/07-hub-train.png" width="384" alt="Train: two kinds of question and objects to quiz"><br><sub>Train: two kinds of question and objects to quiz</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/08-chapters.png" width="384" alt="The book's chapters with square-root share bars"><br><sub>The book's chapters with square-root share bars</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/09-chapters-marquee.png" width="384" alt="Rest on a cut name and it scrolls, after a pause, like a feature phone's list"><br><sub>Rest on a cut name and it scrolls, after a pause, like a feature phone's list</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/10-chapter-list.png" width="384" alt="One chapter's objects, paged by forty"><br><sub>One chapter's objects, paged by forty</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/11-kinds.png" width="384" alt="Every kind of object with its count"><br><sub>Every kind of object with its count</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/12-kind-list.png" width="384" alt="All objects of one kind"><br><sub>All objects of one kind</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/13-detectors.png" width="384" alt="Every object with markers to check"><br><sub>Every object with markers to check</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/14-article.png" width="384" alt="An article: bold title, the kind in a pill, its code"><br><sub>An article: bold title, the kind in a pill, its code</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/15-article-chips.png" width="384" alt="Chips: detector markers and links on the map, one press away"><br><sub>Chips: detector markers and links on the map, one press away</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/16-article-section.png" width="384" alt="Right jumps a section; its heading moves into the header"><br><sub>Right jumps a section; its heading moves into the header</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/17-article-menu.png" width="384" alt="Hold OK: the page's actions over the dimmed page"><br><sub>Hold OK: the page's actions over the dimmed page</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/18-article-contents.png" width="384" alt="Contents: every section, OK jumps there"><br><sub>Contents: every section, OK jumps there</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/19-chapter.png" width="384" alt="A chapter of the book, paged, with next and previous"><br><sub>A chapter of the book, paged, with next and previous</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/20-map.png" width="384" alt="Map of links: four neighbours at a time, confusables dotted"><br><sub>Map of links: four neighbours at a time, confusables dotted</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/21-map-focus.png" width="384" alt="A focused neighbour's full name in the header; OK walks there"><br><sub>A focused neighbour's full name in the header; OK walks there</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/22-detect.png" width="384" alt="Detector: progress cells, one marker at a time: No, Unsure, Yes"><br><sub>Detector: progress cells, one marker at a time: No, Unsure, Yes</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/23-detect-result.png" width="384" alt="Result: your marks as a number and a bar, never a score"><br><sub>Result: your marks as a number and a bar, never a score</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/24-compare-pick.png" width="384" alt="Compare with a look-alike: only those with markers of their own"><br><sub>Compare with a look-alike: only those with markers of their own</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/25-compare-result.png" width="384" alt="Side by side: your marks for both, a count and a bar each, no ranking"><br><sub>Side by side: your marks for both, a count and a bar each, no ranking</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/26-quiz.png" width="384" alt="Which term? The definition with the name masked"><br><sub>Which term? The definition with the name masked</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/27-quiz-options.png" width="384" alt="Distractors are the objects the page calls confusable"><br><sub>Distractors are the objects the page calls confusable</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/28-quiz-result.png" width="384" alt="The answer, the definition, a link to the article"><br><sub>The answer, the definition, a link to the article</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/29-trainer.png" width="384" alt="Manipulative or benign? A catalogue example"><br><sub>Manipulative or benign? A catalogue example</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/30-trainer-result.png" width="384" alt="Why, and a good response, from the catalogue"><br><sub>Why, and a good response, from the catalogue</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/31-review-empty.png" width="384" alt="Review: questions come back by their Leitner box; here nothing is due"><br><sub>Review: questions come back by their Leitner box; here nothing is due</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/32-forget.png" width="384" alt="Forgetting your progress asks first; Back keeps it"><br><sub>Forgetting your progress asks first; Back keeps it</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/33-info.png" width="384" alt="Pack & device: what the pack is and where it came from"><br><sub>Pack & device: what the pack is and where it came from</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/34-info-cache.png" width="384" alt="Measured here: open time, scan time, the read cache's hits"><br><sub>Measured here: open time, scan time, the read cache's hits</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/atlas/35-about.png" width="384" alt="About: what the app is and the keys"><br><sub>About: what the app is and the keys</sub></td><td align="center" width="50%"><img src="docs/evidence/atlas/36-no-pack.png" width="384" alt="No pack: Info and About still work and say what to do"><br><sub>No pack: Info and About still work and say what to do</sub></td></tr>
</table>

Rendered by `tools/atlas.py` from `tests/sim/atlas.txt`: the real pack, the firmware's own u8g2 and fonts, the same app code as the FAP. Every screen of the app has a frame, and CI fails when one is missing or stale ([rule](.ai/repo/rules/project/ui-atlas.v1.md)); `tools/parity.sh` recorded the device drawing identical frames ([evidence](docs/evidence/parity/manifest.json)).
<!-- docs:end atlas -->

## On a real device

Every frame below was **read from the Flipper's screen over USB** by
[`tools/flipper_screen.py`](tools/flipper_screen.py) through the official RPC screen stream.
None of them is a mock-up.

<!-- docs:begin screens -->
<table>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/01-home.png" width="384" alt="Home: six tiles with counts; the status line explains the focused one"><br><sub>Home: six tiles with counts; the status line explains the focused one</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/02-home-detect.png" width="384" alt="Right moves along a row of the grid; the hint follows the focus"><br><sub>Right moves along a row of the grid; the hint follows the focus</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/03-hub-library.png" width="384" alt="Library: chapters, kinds, everything, a random article"><br><sub>Library: chapters, kinds, everything, a random article</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/04-chapters.png" width="384" alt="The book's chapters with square-root share bars"><br><sub>The book's chapters with square-root share bars</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/05-hub-detect.png" width="384" alt="Detect: every detector, or a random one"><br><sub>Detect: every detector, or a random one</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/06-detectors.png" width="384" alt="Every object with markers to check, paged by forty"><br><sub>Every object with markers to check, paged by forty</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/07-article.png" width="384" alt="An article: bold title, the kind in a pill, its code"><br><sub>An article: bold title, the kind in a pill, its code</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/08-chips.png" width="384" alt="Chips: detector markers and links on the map, one press away"><br><sub>Chips: detector markers and links on the map, one press away</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/09-detector.png" width="384" alt="Detector: progress cells, one marker at a time: No, Unsure, Yes"><br><sub>Detector: progress cells, one marker at a time: No, Unsure, Yes</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/10-result.png" width="384" alt="Result: your marks as a number and a bar, never a score"><br><sub>Result: your marks as a number and a bar, never a score</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/11-result-legend.png" width="384" alt="The bar's legend, then what to rule out and the next steps"><br><sub>The bar's legend, then what to rule out and the next steps</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/12-menu.png" width="384" alt="Hold OK: the page's actions over the dimmed page"><br><sub>Hold OK: the page's actions over the dimmed page</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/13-contents.png" width="384" alt="Contents: every section; OK jumps there"><br><sub>Contents: every section; OK jumps there</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/14-map.png" width="384" alt="Map of links: four neighbours at a time, confusables dotted"><br><sub>Map of links: four neighbours at a time, confusables dotted</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/15-map-focus.png" width="384" alt="A focused neighbour's full name in the header; OK walks there"><br><sub>A focused neighbour's full name in the header; OK walks there</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/16-hub-act.png" width="384" alt="Act: what to do, and pairs to tell apart"><br><sub>Act: what to do, and pairs to tell apart</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/17-hub-train.png" width="384" alt="Train: two kinds of question and objects to quiz"><br><sub>Train: two kinds of question and objects to quiz</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/18-quiz.png" width="384" alt="Quiz: which term is this? The name is masked"><br><sub>Quiz: which term is this? The name is masked</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/19-options.png" width="384" alt="Distractors are the objects the page calls confusable"><br><sub>Distractors are the objects the page calls confusable</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/20-answer.png" width="384" alt="The answer, the definition, a link to the article"><br><sub>The answer, the definition, a link to the article</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/21-trainer.png" width="384" alt="Trainer: manipulative or benign? A catalogue example"><br><sub>Trainer: manipulative or benign? A catalogue example</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/22-trainer-answer.png" width="384" alt="Why, and a good response, from the catalogue"><br><sub>Why, and a good response, from the catalogue</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/23-info.png" width="384" alt="Pack and device: source, contents, measured times"><br><sub>Pack and device: source, contents, measured times</sub></td><td align="center" width="50%"><img src="docs/evidence/pocket/24-info-cache.png" width="384" alt="The read cache on the device: hits and misses"><br><sub>The read cache on the device: hits and misses</sub></td></tr>
  <tr><td align="center" width="50%"><img src="docs/evidence/pocket/25-about.png" width="384" alt="About: what the app is and the keys"><br><sub>About: what the app is and the keys</sub></td></tr>
</table>

Captured 2026-11-25 on firmware 1.5.1 with `tools/capture-screens.sh`.
<!-- docs:end screens -->

The text renderer ([`app/reader.c`](app/reader.c)) is written for this 128 × 64 display. It
wraps lines by real glyph widths and draws a ruled title, inverted section labels,
hanging-indent bullets, quoted notes, selectable links and a button bar. The firmware's fonts
are ASCII-only (measured: [`docs/evidence/fonts`](docs/evidence/fonts)), so the compiler folds
Czech diacritics for the device and keeps the original text everywhere else.

## Small memory, by design

A Flipper Zero gives an app a few tens of kilobytes of heap, shared with the firmware, and an
SD card read over SPI. The app follows the patterns of James Noble and Charles Weir's
*Small Memory Software* (Addison-Wesley, 2001), and every one of them below is in the code, not
only in this text:

| Pattern | Where it is |
|---|---|
| **Read-only data file** | The book lives in a `.pkp` on the SD card, not in the app. Adding an article changes the pack, never the FAP ([acceptance test](docs/architecture.md)). |
| **Fixed allocation** | Every buffer is allocated once at start: the page's text, blocks and lines (`TEXT_CAP`, `BLOCK_CAP`, `LINE_CAP` in [`page.c`](app/ui/page.c)), one string scratch, the frame stack, the cache. Nothing is allocated while you read or draw. |
| **Memory limit** | Each buffer has a cap, and a page that would exceed it ends with *"more in the pack than fits on one page"* instead of failing. |
| **Paging** | Only the records on screen are read: one object (40 bytes), its items, its strings. Lists read forty rows at a time. |
| **Caching** | An LRU cache of eight 256-byte blocks (2 KiB) sits in front of every read ([`cache.c`](app/core/cache.c)). Its hit rate is on the Info screen. |
| **Sharing** | The compiler stores each distinct string once ([`tools/pocket.py`](tools/pocket.py) `Strings`); records point into one pool. |
| **Packed data** | Fixed-width little-endian records, read field by field, never cast to C structs; every offset is bounds-checked. |
| **Partial failure** | A corrupted record or a pulled SD card shows *"could not be read"* on that screen, and the rest of the app keeps working (scenarios `eject-*`, `bad-*`). |
| **Two copies, one winner** | Quiz progress (Leitner boxes, [`progress.c`](app/core/progress.c)) is a bounded, versioned, checksummed file of at most 3 KiB. A save writes a temporary copy, then replaces the file; the reader takes the valid copy with the higher generation, so a torn write or a rename that never happened costs at most the last answer (scenarios `progress-*`). |
| **Exhaustion test** | The parser is fuzzed with thousands of mutated packs under ASan and UBSan, and checked against an independent Python decoder. |

### The cache, measured

The same walk through the app (the device tour of `tools/capture-screens.sh`) over the real
pack, once through the cache and once reading the card directly:

<!-- docs:begin cache-bench -->
| The same walk | Card reads | Bytes from the card |
|---|---:|---:|
| Reading the card directly | 9 890 | 967 257 |
| Through the cache (8 x 256 B) | 4 060 | 1 038 520 |

The cache served 9 440 of 13 500 block requests from memory: 59% fewer card reads for +7% bytes, since a miss reads a whole block. Walk: 34 key presses (tour of tools/capture-screens.sh); measured by `tools/cache-bench.sh`.
<!-- docs:end cache-bench -->

On the device, the Info screen keeps the same counters for the session
([frame from the device](docs/evidence/pocket/24-info-cache.png)). Fewer, larger reads suit an SD
card, where each read pays a command round trip; the time saved per read has not been measured
yet, so no speed-up is claimed.

## What is inside

The pack is compiled from the Kompendium at a pinned commit. These numbers come from the pack
build report and are regenerated by [`tools/docs.py`](tools/docs.py). CI refuses a README whose
numbers disagree with the build.

<!-- docs:begin pack-stats -->
| | |
|---|---:|
| Knowledge objects | **2 224** |
| Categories | **64** |
| Typed relations (links between objects) | **37 567** |
| Objects with a detector (observable markers) | **1 212** |
| Objects with what-to-do steps | **1 541** |
| Objects with confusables to rule out | **1 128** |
| Quiz questions | **2 856** |
|   of which: recognise the term from its definition | **1 671** |
|   of which: manipulative or benign? (catalogue examples) | **1 185** |
| Markers (indicators) | **4 246** |
| Actions and escalation steps | **4 558** |
| Chapters | **16 789** |

Pack `kompendium.pkp`: 20 649 388 bytes, sha256 `6752496025b2cdb6…`, rebuilt byte-for-byte identical by CI from the pinned source.
<!-- docs:end pack-stats -->

<details>
<summary>Object kinds</summary>

<!-- docs:begin pack-kinds -->
| Kind | Objects | Share |
|---|---:|---|
| `article` | 552 | `██████` 24.8 % |
| `technique` | 503 | `█████` 22.6 % |
| `pattern` | 420 | `█████` 18.9 % |
| `concept` | 289 | `███` 13.0 % |
| `fallacy` | 147 | `██` 6.6 % |
| `scenario` | 120 | `█` 5.4 % |
| `protocol` | 80 | `█` 3.6 % |
| `countermeasure` | 50 | `█` 2.2 % |
| `signature` | 33 | `█` 1.5 % |
| `exercise` | 30 | `█` 1.3 % |
<!-- docs:end pack-kinds -->

</details>

## How it works

```mermaid
flowchart LR
  subgraph source["Kompendium (pinned commit)"]
    P[Markdown pages<br/>TOML front matter]
    C[fallacy and NLP catalogues<br/>with example utterances]
  end
  subgraph compiler["tools/pocket.py"]
    D[discover + extract<br/>heading → role map]
    M[merge<br/>cards → techniques<br/>catalogue → pages]
    R[resolve links<br/>typed relations]
    Q[derive quizzes<br/>masked definitions,<br/>confusable distractors]
    E[encode PKP1<br/>deterministic bytes]
  end
  subgraph device["Flipper Zero"]
    SD[(SD card<br/>kompendium.pkp)]
    CORE[app/core<br/>bounded reader,<br/>no Furi]
    UI[reader · detector ·<br/>applicator · trainer]
  end
  P --> D
  C --> M
  D --> M --> R --> Q --> E --> SD --> CORE --> UI
```

**One declarative map, no per-topic code.** [`knowledge/sources.json`](knowledge/sources.json)
says which collections become which kind of object and which headings play which role. For
example, *Jak ji poznat* and *Pozorovatelné markery* become markers, *První bezpečná reakce*
becomes actions, and *S čím ji nezaměnit* becomes confusables. A quick-reference card merges into
its technique by EA code, and a catalogue entry merges into its page by catalogue id. Whatever a
page yields decides what the app offers for it.

**A pack is untrusted input.** The reader in [`app/core/pkp.c`](app/core/pkp.c) is portable C99
with no allocation and no dependency on the firmware. It checks the header, the index CRC and
every section's bounds when it opens a pack, and then every record and string as it reads it.
No two sections may overlap, nothing is cast from file bytes to a struct, and the names of kinds
and relations come from the pack itself. The string pool has its own checksum, verified on
request; a damaged string can only show wrong text, never read out of bounds. Checksums detect
corruption; they authenticate nothing.

**The device holds one record at a time.** Lists are paged, articles are assembled from records
read on demand through a 2 KiB LRU block cache ([measured](#the-cache-measured)), and the open
time, index scan time and cache hits are shown on the device (*Info*).

More: [architecture](docs/architecture.md) · [development](docs/development.md) ·
[case study: from zero to app](docs/case-study.md).

## Install

You need a Flipper Zero on official firmware 1.5.x (or Momentum mntm-012) and an SD card.

1. From [Releases](https://github.com/korczis/prismatic-flipperzero/releases), download the FAP
   whose API matches your firmware and the `kompendium-*.pkp` pack, and verify them against
   `SHA256SUMS`.
2. Copy the FAP to `SD/apps/Tools/` and the pack to `SD/apps_data/prismatic/` (qFlipper or the
   mobile app).
3. Open **Apps → Tools → Prismatic**.

Controls: **Up/Down** scroll, **Left/Right** jump between links, **OK** opens a link, **Back**
goes one step back. In the detector, **Left** is *no*, **Right** is *yes*, **OK** is *unknown*,
and holding **OK** means *not applicable*.

## Build it yourself

```bash
just doctor        # host toolchain, SDKs, GitHub, device, Majordomus: each one READY or not
just sdk           # pinned official SDK, SHA-256 checked
just pack          # extract the pinned Kompendium and compile build/packs/kompendium.pkp
just deploy-pack   # copy the pack to the SD card
just launch        # build, install and start the app
just verify        # everything CI checks, locally
```

| Recipe | What it does |
|---|---|
| `just pack` | Compiler: pinned sources → IR → `.pkp`, plus a build report |
| `just test` | Tool tests and the gate that fails on any line of `tools/*.py` no test ran |
| `just test-native` | Portable core under ASan/UBSan, mutation fuzzing, C-vs-Python differential, llvm-cov line gate |
| `just build [sdk]` / `just build-all` | FAP for official firmware or Momentum, with provenance JSON |
| `just launch` / `just deploy` / `just deploy-pack` | Device operations behind a lock, so two shells never drive one Flipper |
| `just screenshot` / `just walk DIR KEYS` | Real device frames, optionally after scripted key presses |
| `just release-assets` | Versioned FAPs, the pack, provenance and `SHA256SUMS` |

## How it is kept honest

- **Derived, then checked.** The compiler is deterministic: CI compiles the pack twice and
  compares the bytes. README numbers, the screenshot gallery and the case-study timeline are
  generated, and CI fails when they drift.
- **Every line tested.** The gate fails on any executable line of `tools/*.py` that no test ran.
  For the portable C core, the llvm-cov gate does the same for every line with code. Each gate
  has a self-test proving it refuses an untested line.
- **The parser is attacked on purpose.** Thousands of mutated packs run under AddressSanitizer
  and UndefinedBehaviorSanitizer, and the C reader and the Python reference must accept and
  reject exactly the same ones (zero disagreements is the bar).
- **Hardware claims need hardware evidence.** Launch, navigation, detector and quiz flows are
  verified on a real device with captured frames ([evidence](docs/evidence)).
- **Supervised.** [Majordomus](https://majordomus.dev) holds the policy in `.ai/`, generates the
  agent instructions, runs `doctor` before every commit and the finish contract before every
  push, and CI runs a pinned `doctor`. Agents start from [`AGENTS.md`](AGENTS.md), and the
  project rules are in [`.ai/repo/rules/project/`](.ai/repo/rules/project/). During development it refused a commit and a task finish
  that did not meet the contract ([case study](docs/case-study.md)).
- **Reviewed by more than one mind.** The pack reader was reviewed by an independent peer
  session and by Codex; their findings are fixed and listed in the history.

## Firmware compatibility

| Firmware | API | State |
|---|---|---|
| [Official 1.5.1](https://github.com/flipperdevices/flipperzero-firmware/releases/tag/1.5.1) | 88.2 | built in CI, verified on hardware |
| [Momentum mntm-012](https://github.com/Next-Flip/Momentum-Firmware/releases/tag/mntm-012) | 87.1 | built in CI, not yet run on hardware |

Momentum's maintainers don't accept AI-generated contributions, so this project builds against
Momentum's published SDK and never changes the firmware itself.

## Roadmap

```mermaid
timeline
  title Prismatic Pocket
  v0.0.1 First Light : native FAP on hardware : pinned SDK matrix : preview release
  v0.1 Pocket : full Kompendium pack : reader, detector, applicator, trainer : CI for core and pack
  next : search index : multi-pattern detector with shared markers : sessions and spaced repetition
  later : host bridge : Prismatic adapter : more packs from the same compiler
```

## Licence

The code (runtime, compiler, tools) is [MIT](LICENSE). The knowledge content is the Kompendium
author's work, included by the author's grant; the catalogue's example utterances are included by
the author's explicit decision. Both decisions are recorded in [`knowledge/LICENSE.md`](knowledge/LICENSE.md)
and `.ai/repo/adrs/`. The prism mark is original and generated by [`tools/icons.py`](tools/icons.py).
Flipper Zero is a trademark of Flipper Devices Inc.; this project is not affiliated with it.
