# From zero to a working app: a case study

> One session started from an empty directory and ended with a public repository and a native
> Flipper Zero application. The app holds an entire book as a searchable, interactive knowledge
> pack, runs on real hardware and is covered by tests that fail on any untested line. This page
> records how that went, what made it fast, and what refused to let it be fast in the wrong way.

## The starting point

There was no repository, no app, no SDK pinned and no idea of what the knowledge format would
look like. There was a Flipper Zero, which turned out to be sitting in DFU recovery mode, a
public book (the Kompendium) on the same machine, and a Mac with the owner's usual tooling:
uFBT, Rust, Python, `just`, the GitHub CLI and [Majordomus](https://majordomus.dev).

The brief changed four times during the session, each time getting larger:

1. A Flipper app with a host bridge.
2. *First Light*: get your own app onto the device before anything else.
3. *Prismatic Pocket*: a data-driven epistemic engine.
4. *All of the Kompendium*, as an encyclopedia and interactively, with docs, Pages and a wiki.

A process that needs a stable spec would have stalled at step 2.

## The timeline

Generated from the repository's history by [`tools/docs.py`](../tools/docs.py). Times are local.

<!-- docs:begin timeline -->
| Commit | Time | What landed |
|---|---|---|
| `28da2ca` | 03:41 | chore(repo): bootstrap governed repository with Majordomus layer |
| `e64127a` | 04:01 | feat(app): first light, a native Prismatic dialog verified on hardware |
| `67c2cec` | 04:05 | docs(repo): README with real device frames, CI matrix and preview release flow |
| `f85d96e` | 05:08 | feat(pocket): full Kompendium as a knowledge pack and a generic runtime |
| `914e20f` | 05:40 | feat(pocket): structured reader, article navigation and CI for the core and pack |
| `92e5bf0` | 06:04 | docs(pocket): prose README, case study, architecture, real device gallery |
| `fbc955c` | 06:10 | style(app): apply the SDK clang-format to app sources |
| `55fc96d` | 06:31 | feat(site): Pages explorer over the full pack, tested end to end |
| `d25ff11` | 06:34 | feat(docs): render the GitHub wiki from README and docs |
| `a1cc740` | 06:36 | docs(readme): link the online explorer and the wiki |
| `4fb013a` | 06:39 | fix(site): ignore the gallery when the overview is already gone |
| `d9d7a20` | 07:10 | fix(tests): docs and wiki tests no longer read the real README |
| `3e6c165` | 08:13 | feat(ui): portable interface, host simulator with firmware fonts, device parity |
| `f015e92` | 08:31 | feat(ui): context menu, contents outline, map of links, calmer paragraphs |
| `152440e` | 08:35 | fix(sim): declare POSIX for strdup under -std=c11 on glibc |
| `2b265f8` | 09:15 | feat(ui): one type family, kind pill, action chips, section icons, row bars |
| `ffdc599` | 09:23 | docs(readme): interface gallery from the simulator, device gallery recaptured |
| `5010087` | 09:31 | feat(core): an LRU block cache in front of every pack read |
| `3908db4` | 09:45 | feat(ui): information architecture of four hubs on a tile grid |
| `641a0a5` | 10:08 | feat(ui): screen atlas of every screen, reviewed; cache measured; docs |
| `a3e2211` | 10:14 | feat(site): the device page, every screen of the app from the atlas |
| `69840eb` | 10:25 | test(acceptance): a new object, only the pack recompiled, the same FAP, on the device |
| `aec317c` | 10:26 | fix(atlas): compare frames by decoded pixels, not PNG bytes |
| `1340051` | 10:43 | feat(ui): a cut name scrolls after a pause; the quiz keeps score |
| `c9fa1c6` | 10:47 | fix(ui): paging position fits two 32-bit counts; build the sim with GCC too |
| `b40edb4` | 10:49 | docs(evidence): parity 24 of 24 on the marquee and quiz-score FAP |
| `7480076` | 11:49 | feat(train): spaced repetition with Leitner boxes, kept safely on the SD card |
| `d40840a` | 12:00 | feat(detect): side-by-side detection of an object and a look-alike |
| `81a28e1` | 12:17 | feat(apply): assessments as applications: Admiralty code, DD evidence levels, OSINT confidence |
| `e0830aa` | 12:18 | test(tools): validate the repository's applications only where the file exists |
| `f87e677` | 12:25 | docs(readme): FAEL-style landing: title, tagline, generated explainer and modules |
| `619187f` | 12:30 | feat(site): assessments are interactive on the explorer too |
| `aa9bd09` | 13:09 | feat(sessions): save a detector's marks as JSON Lines, only when asked |
| `5e934ff` | 13:11 | fix(sessions): drop an unreachable empty-list note; restore covers an empty page |
| `3761ede` | 13:25 | feat(simulate): the Prismatic Platform's crisis simulations, as data, offline |
| `56fadfc` | 13:35 | feat(score): the Prismatic Platform's scorecards as data: executive frame, nominee risk |
| `30d303a` | 13:44 | feat(calculate): the Prismatic Platform's Bayesian fusion as an interactive calculator |
| `e694aa4` | 13:46 | fix(calculate): single precision for the Flipper's FPU |
| `b528ea4` | 14:30 | feat(site): simulations, scorecards and calculators are interactive in the browser |
| `7fc8536` | 14:42 | feat(ui): live tools on the device: steppers change values in real time |
| `169ba75` | 14:47 | feat(calculate): one stepper adds a source; the calculator fits one screen |
| `0ff2236` | 14:57 | docs(evidence): device parity recorded with the live tools build |
| `dfcbdc8` | 12:16 | chore(release): 0.1.0 Pocket: version, changelog, release notes, security scope |
| `7f2bc73` | 12:19 | docs(evidence): parity 24 of 24 and device gallery recaptured on 0.1.0 |
| `005e714` | 12:39 | feat(bridge): host bridge over the Flipper's own RPC |
| `29db801` | 13:20 | fix(bridge): ping until the RPC session answers; record the device run |
| `b33816e` | 13:39 | fix(bridge): push-pack over acknowledged CLI chunks; never end a busy session |
| `85f461a` | 13:47 | fix(bridge): install a pack in one CLI session, never mixing RPC and CLI |
| `c8d58d7` | 13:56 | docs(evidence): bridge device run: status and small installs proven |
| `8f53906` | 14:16 | fix(bridge): never release the port while the device is still working |
| `8413a03` | 14:16 | docs(evidence): the bridge proven on the device, end to end |
| `8755d42` | 14:18 | chore(just): run the bridge recipe without echoing its shell line |
| `24ee70d` | 14:38 | docs(adr): ADR 0001-0005 accepted by the owner; UI workflow invokes the Flowbite skill |
| `3c8502a` | 14:42 | chore(release): 0.2.0 Bridge: version, changelog, release notes, security |
| `1a6f1d5` | 14:46 | docs(evidence): parity 24 of 24 on the 0.2.0 build |
| `0fc3fd4` | 14:50 | docs(release): v0.2.0 notes say which build the device gallery shows |
| `64de98d` | 14:55 | fix(screen): end an RPC session only after the device confirms it |
| `7ee9310` | 15:31 | docs(evidence): device gallery recaptured on the 0.2.0 build |
<!-- docs:end timeline -->

## What made it fast

### 1. Governance from minute one, not at the end

The first commit was not code. It was `majordomus init` and `update`, and the generated
`AGENTS.md`, `CLAUDE.md` and `GEMINI.md`. Tracked hooks ran `doctor` before every commit and the
finish contract before every push. That sounds like overhead, but it is what let the work move
quickly without losing the thread:

- **Refused commit.** The very first commit attempt was refused because the README did not name
  `AGENTS.md`. That took ten seconds to fix and taught nothing new, but it proved the gate was
  real before anything depended on it.
- **Refused finish.** A task `finish` was refused because a file outside the task's declared
  scope had been touched (the uFBT skeleton). The task was closed as a handover instead of being
  called complete. The record says what really happened, not what would have looked tidy.
- Each brief change became a new scoped task, so the history shows where one direction ended
  and the next began.

### 2. Evidence instead of belief

Every claim in this repository is meant to have a way to check it, and the session kept running
into reasons why:

- **The device.** The Flipper was in DFU mode. The official repair crashed once and lost USB once
  before it succeeded on the third attempt. All three outcomes are recorded, not just the last.
- **The fonts.** A thirty-second probe app showed that the firmware's fonts cannot render a
  single Czech diacritic: UTF-8 breaks the line. That measurement, not an assumption, is why the
  compiler folds text to ASCII for the device.
- **The screen.** Screenshots come from the device's own RPC screen stream. A dedicated tool reads
  and writes the frames, so the README shows what the hardware actually displays.
- **Loader vs RPC.** An app started over RPC is closed when any later RPC session ends. This was
  found by watching the app vanish between two screenshot runs, and `just launch` now starts it
  with the loader instead.

### 3. A compiler, not a content team

The decisive design choice: the app knows nothing about manipulation. A compiler turns every
page of the book into typed objects, using a declarative map of which heading plays which role.
Quick-reference cards merge into their techniques, catalogue entries into their pages, and links
become typed relations. Quizzes are derived: definitions with the answer masked, distractors
chosen from the objects a page itself calls confusable. Scaling from one hand-picked object to
the whole book was a configuration change plus a rerun, not weeks of writing.

### 4. More than one mind on the risky parts

- **Codex**, consulted as an independent reviewer through Majordomus's advisor registry, pointed
  out three things that changed the design: quick-reference cards duplicate techniques and must
  merge; prose headings are not reliable fields; and the book deliberately does not publish its
  trainer utterances. The last point went to the owner, who decided to publish them, and that
  decision is recorded verbatim in an ADR.
- **A peer Claude session** working in the same workspace reviewed the binary pack reader
  adversarially. It found a real bug (the reader looked for the string-pool checksum at the wrong
  header offset), a real hole (a crafted section layout could move tables out from under the
  index checksum), and a test that was quietly testing nothing. All three are fixed, and the
  vacuous test now refuses to pass vacuously.
- **Gemini** was asked too. Its CLI refused to run (the client is no longer supported), and that
  is recorded as well.

### 5. Tests that cannot be gamed by omission

The owner's rule is that every changed line ships with a test that runs it. Here that is
enforced, not encouraged:

- A coverage gate for the Python tools uses `sys.monitoring` with no third-party dependency, and
  a self-test proves it refuses an untested line.
- The C core runs under ASan and UBSan, is mutation-fuzzed against the Python reference until
  the two agree on every accept and reject, and has an llvm-cov gate over every line with code.
- CI compiles the knowledge pack twice and compares the bytes.

## What the numbers say

The pack this session produced:

<!-- docs:begin pack-stats -->
| | |
|---|---:|
| Knowledge objects | **2 241** |
| Categories | **66** |
| Typed relations (links between objects) | **37 567** |
| Objects with a detector (observable markers) | **1 212** |
| Objects with what-to-do steps | **1 548** |
| Objects with confusables to rule out | **1 128** |
| Quiz questions | **2 856** |
|   of which: recognise the term from its definition | **1 671** |
|   of which: manipulative or benign? (catalogue examples) | **1 185** |
| Markers (indicators) | **4 196** |
| Actions and escalation steps | **4 567** |
| Chapters | **16 807** |

Pack `kompendium.pkp`: 20 705 248 bytes, sha256 `b2b7788af2f9b574…`, rebuilt byte-for-byte identical by CI from the pinned source.
<!-- docs:end pack-stats -->

## What it would have taken otherwise

The honest comparison is not "faster than a human team". It is *what the supervision layer
prevented*:

- A green-looking repository with untested code. The gates refuse it.
- A README claiming numbers nobody measured. The doc regions are derived and checked.
- A hardware claim based on a successful upload. Launch, navigation and flows are shown in
  captured frames.
- A licence problem shipped silently. The Kompendium's licence conflict was surfaced, and the
  owner's grant recorded before any text was copied.
- A design that needed C changes for every new topic. It doesn't, and the compiler is the proof.

Speed came from not having to go back.
