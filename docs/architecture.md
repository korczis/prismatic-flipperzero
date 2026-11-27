# Architecture

Prismatic Pocket has three parts with one contract between each pair:

```mermaid
flowchart LR
  K[(Kompendium<br/>pinned commit)] -->|git archive| C[compiler<br/>tools/pocket.py]
  C -->|IR JSON| C
  C -->|PKP1 pack| SD[(SD card)]
  SD -->|bounded reads| R[portable reader<br/>app/core]
  R --> U[Flipper UI<br/>app/prismatic.c + app/reader.c]
```

## 1. Compiler (`tools/pocket.py`)

`knowledge/sources.json` declares everything that varies:

- **Pages**: the roots to read and the files to skip.
- **Kinds**: which collection becomes which object kind (`techniques` → `technique`,
  `detection-signatures` → `signature`, `glossary` → `concept`, everything else `article`).
- **Roles**: which section heading plays which role: `definition`, `indicators`, `actions`,
  `escalation`, `alternatives`, `validation`, or `skip`. Any other heading becomes a chapter.
- **Overlay**: quick-reference cards merge into the technique with the same EA code.
- **Catalogues**: fallacy and NLP TOML entries merge into the page with the same catalogue id
  (or their canonical page). They contribute aliases and example utterances.
- **Templates**: a page's source is a Zola (Tera) template. Shared passages are
  `{% include "partials/…" %}`, the technique metadata row renders from front matter and
  `data/*.toml`, and index pages loop over the derived listings in `static/data/listings/`. The
  compiler renders exactly that subset (include, `set`, `set_global`, `load_data`, `for`, `if`,
  `+`, the `default` and `length` filters) from the same commit, so the pack holds the page a
  reader gets; any other tag refuses the page, and browser-only shortcodes render to nothing.

The pipeline runs in a fixed order:

1. Discover (one `git archive` of the pinned commit).
2. Extract (templates rendered, then front matter plus sections, with tables and lists as items).
3. Merge.
4. Resolve links into typed relations (`confused_with` for links inside a *don't confuse*
   section, `related_to` for the rest).
5. Validate.
6. Derive capabilities and quizzes.
7. Encode.

The output is deterministic: identical inputs produce identical bytes.

Capabilities are derived, never declared:

- `detect` if the object has markers.
- `apply` if it has actions or escalation.
- `compare` if it has confusables.
- `quiz` if a question could be derived.

## 2. Pack format (PKP1)

The format is little-endian throughout, and every integer is decoded explicitly.

| Part | Content |
|---|---|
| Header (32 B) | magic `PKP1`, version, required-feature flags, file size, **index CRC**, section count, object count, **string-pool CRC** |
| Section directory | `{type, entry size, offset, length, count}` per section |
| META | pack id, title, version, source repository and commit, generator, licence |
| STRINGS | deduplicated ASCII text, referenced as `(offset, length ≤ 2048)` |
| CATEGORIES | `{name, first object, count}`, objects grouped by category |
| OBJECTS | 40-byte records: id, code, title, definition, category, kind, capabilities, item range, relation range |
| ITEMS | `{type, text}`: marker, action, escalation, alternative, validation, chapter heading, chapter text |
| RELATIONS | `{type, target object}` |
| QUIZ | 34-byte records: kind, answer, four options, prompt, context, explanation, response |
| KINDS, RELNAMES | the vocabularies, so a new kind needs no new firmware |
| SEARCH_KEYS (10, optional) | 16-byte keys of Find by name: up to ten characters of `[a-z0-9]`, NUL-padded, strictly ascending, each with the first posting (u32) and a count (u16) of its run; the runs are contiguous |
| SEARCH_POSTINGS (11, optional) | u16 per (key, object): bits 0-14 the object, ascending within a run; bit 15 a strong match (the title's first word after a leading code, or the code) |

The index CRC covers everything after the header except the string pool and is checked when a
pack is opened; the search index lies outside the pool, so the CRC covers it too. The pool is
most of the file and has its own CRC, checked on request; corrupted text cannot escape its
bounds. No two sections may overlap. A pack with unknown required-feature flags is refused.

Sections 1-9 are required; 10 and 11 come together or not at all. A reader skips any section
type it does not know, which is how the format grows without a version bump: a required-feature
flag is only for data an old reader would misread, and a changed layout of an existing section
takes new section types instead of a new version. So the v0.2.0 app opens a pack with a search
index and simply offers no search, and this app opens a v0.2.0 pack and says it has no index;
`tests/native/run.sh` builds the v0.2.0 reader and compiler from their tag and proves both.
Types 12-14 are reserved for a multi-pattern detector; the next free type is 15.

The index holds the words of each title and its aliases, the code's words and the code without
its punctuation (`EA-3.2.F` also as `ea32f`), folded to ASCII, lower case, two characters at
least (the compacted code any length), cut to ten characters, colliding keys merged. Definitions
are not indexed. A pack of more than 4 096 objects gets no index: the device's matches are two
4 096-bit sets.

## 3. Runtime (`app/`)

- **`app/core/`**: portable C99 with no Furi and no allocation. It holds the pack reader
  (`pkp.c`), the LRU block cache every read goes through (`cache.c`: eight 256-byte blocks), and
  the detector summary and quiz check (`pocket.c`), and quiz progress for spaced repetition
  (`progress.c`: Leitner boxes in a bounded, checksummed file kept as two copies), and Find by
  name (`search.c`: a query typed a letter at a time is two lower-bound searches inside the
  previous key range and one pass over a contiguous postings run; the letters that can follow are
  a 64-bit mask, the matches two bitsets, 1.2 KiB in all). It is tested on the host under
  sanitizers, and `search.c` against `tools/pocket.py search` on every prefix of one to three
  letters and every key typed in full.
- **`app/ui/`**: the whole user interface, portable as well. It uses only the canvas API and C99.
  - `page.c` is the one view engine. A page is a pinned header (the title, or the section whose
    heading scrolled by, plus the position), a body of styled blocks and optional softkeys.
    Documents and lists are both pages, and every design rule lives in its tables
    ([`docs/ui.md`](ui.md)).
  - `ui_app.c` is the app: a bounded frame stack and every screen as a function of the pack.
  - `icons.h` is generated by `tools/icons.py`.
- **`app/prismatic.c`**: the Furi glue and nothing else. It opens the pack through the shared
  block cache, forwards keys, draws, and quits on Back from Home or on the loader's exit signal.

### The simulator

`tests/sim` builds the same `app/ui` and `app/core` on the host against the firmware's own
u8g2 renderer and fonts. The sources are fetched from firmware 1.5.1 and checked against
`tests/sim/u8g2.sha256`. A key script drives the app and produces the frames.

- `tools/parity.sh` walks the same keys on the device and in the simulator and compares every
  frame. The last recorded run, in [`docs/evidence/parity/`](evidence/parity/manifest.json),
  found every frame identical.
- `tests/sim/run.sh` runs every scenario in `tests/sim/scenarios.txt` under ASan and UBSan,
  including corrupted packs and an SD card pulled mid-use. Each scenario asserts the visible
  text, and the run fails on any line of `app/ui` that no scenario ran.

## The acceptance test

The architecture stands or falls on one property: adding a knowledge object with capabilities
the app already supports must not touch the app. `tools/acceptance.sh` proves it end to end. It
adds one canonical object ([`tests/acceptance/object.json`](../tests/acceptance/object.json), a
test fixture that never enters the published pack) to the extracted IR, recompiles only the
pack, rebuilds the FAP from the unchanged `app/` and requires the same sha256, then walks to the
object through the generic UI (Library, By chapter, its chapter, its row), runs its detector and
answers its quiz, asserting what the screen shows. The key presses are derived from the pack, not
written down. With `--device` it repeats the walk on the Flipper with the pack deployed as
`acceptance.pkp`, compares every frame with the simulator's and removes the test pack again.
`tools/verify.sh` runs the host part on every verification. The last device run:

<!-- docs:begin acceptance -->
| Step | Result |
|---|---|
| Add one canonical object | [`tests/acceptance/object.json`](../tests/acceptance/object.json) |
| Recompile only the pack | `33a77299bfce50a6` |
| Rebuild the FAP from the unchanged `app/` | the same sha256, `25c82ce4599ee3e9` |
| Find it, run its detector, answer its quiz (simulator) | found through Library > By chapter, detector 2/3 markers, quiz correct |
| The same 34 key presses on the Flipper (firmware 1.5.1) | 35 frames identical to the simulator, 0 different |

Run 2026-11-25 by `tools/acceptance.sh --device`; frames from the device: [result.png](evidence/acceptance/result.png), [answer.png](evidence/acceptance/answer.png).
<!-- docs:end acceptance -->

## Boundaries

- The device never decides anything about a person. The detector returns the operator's own
  marks and the book's guidance on what to rule out and what to do next.
- Packs are data. Nothing in a pack is executed.
- The FAP does not depend on the pack's content: a new pack needs no new firmware build.
