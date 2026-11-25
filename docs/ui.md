# The interface

Prismatic Pocket runs on a 128 × 64 monochrome LCD. It has five buttons and Back, and its bitmap
fonts are about 7 px tall. This page describes the design system in `app/ui/page.c`, explains
why each rule exists, and shows how the interface is tested.

## Typography

There are three roles, all from one family:

- **Titles and section headings:** helvB08, the firmware's bold FontPrimary.
- **Reading text:** helvR08, its regular weight. It is embedded in the FAP from the pinned
  firmware's u8g2 by `tools/fonts.py`, because the firmware exports no fonts to apps.
- **Captions:** haxrcorp4089. Used for meta lines, notes, counts, legends and the kind pill.

The regular weight has a real x-height and reads at 11 px pitch. That gives four lines of text
under the header instead of five cramped ones, a trade made for readability.

## One engine, two shapes

Every screen is a **page**. A page has three parts:

- a pinned header;
- a body of styled blocks;
- optional softkeys.

Documents (articles, results, chapters) and lists (menus, options) are both pages. A list is a
page of rows. The look is therefore decided in one place, and a new screen is a new arrangement
of existing blocks, not new drawing code.

| Block | Look | Used for |
|---|---|---|
| Title | bold, wrapped | an article's own name |
| Meta | small line with a dotted rule under it | kind and code, captions |
| Heading | full-width inverted bar, bold | sections; Left/Right jump between them |
| Paragraph / Bullet | regular, 10 px pitch, hanging indent for bullets | text, markers, steps |
| Note | bar on the left | warnings, the detector's "marks only" note |
| Quote | rounded frame | a catalogue utterance in the trainer |
| Link | chevron; focused = inverted rounded box | actions, related objects, chapters |
| Row | icon, label, right-aligned count | menus, lists, quiz options |
| Big | number in the 22 px digit font plus a caption | detector result |
| Bar | stacked bar (solid, empty, dots, hatch) with a legend | the share of each mark |
| Steps | one cell per step, current one framed | detector progress |
| Badge | the kind in tracked capitals inside an inverted pill, then the code | an article's identity |
| Chip | rounded box with an icon and a count; focused = inverted | an article's actions: detector, quiz, map |
| Row bar | 14 px mini bar before a row's count (square-root scale) | home and category sizes |

Two more surfaces sit on top of pages:

- **The context menu.** Hold OK on any page without softkeys. A panel opens over the stippled
  page with the page's actions: the detector, the quiz, the map of links, the category and
  Home. The engine adds **Contents** whenever the page has two or more sections. Contents lists
  the page's own headings, and choosing one moves that heading into the header.
- **The map of links.** This is a bounded local graph: the object in the centre and four
  neighbours around it (top, right, bottom, left). Confusable objects sit on dotted edges and
  related ones on solid edges. The focused neighbour's full name replaces the title in the
  header, and the header counts the windows of four ("2/3").

## Geometry

The table below is read from the engine's `#define`s by `tools/docs.py`. Do not edit it by hand.

<!-- docs:begin ui-geometry -->
| Constant | Value | Meaning |
|---|---:|---|
| `TEXT_CAP` | 12288 | text per page |
| `BLOCK_CAP` | 220 | blocks per page |
| `LINE_CAP` | 900 | laid-out lines per page |
| `SCREEN_W` | 128 px | display width |
| `SCREEN_H` | 64 px | display height |
| `HEADER_H` | 13 px | pinned header (title, position, rule) |
| `SOFT_H` | 12 px | softkey bar |
| `LEFT` | 2 px | left text margin |
| `RIGHT` | 123 px | last text column (scrollbar right of it) |
| `LEADING` | 11 px | body line pitch |
| `ROW_H` | 12 px | list row |
| `HEADING_H` | 13 px | section heading bar |
| `BIG_H` | 24 px | big-number result line |
| `BAR_H` | 21 px | stacked bar with legend |
| `GAP` | 3 px | space after a paragraph |<!-- docs:end ui-geometry -->

## Navigation contract

| Key | Documents | Lists | With softkeys |
|---|---|---|---|
| Up / Down | next link in view takes the focus, else the text moves one line | next row (wraps) | scroll |
| Left / Right | previous / next section, whose heading and icon move into the header; on a row of chips, the next chip | four rows up / down | left / right softkey |
| Hold Up / Down | same as Left / Right | same | same |
| OK | open the focused link | open the row | centre softkey (hold = its long action) |
| Back | one step back, restoring the scroll and the focused row | same | same |
| Hold OK | context menu (Back or holding OK again closes it) | context menu | the softkey's long action |

On the map, a direction focuses the neighbour on that side. The same direction again shows the
next four neighbours, and the opposite direction returns to the centre. OK on a neighbour
re-centres the map there, so you can walk the book's links. Holding OK opens the neighbour's
article, and OK on the centre opens the centre's article.

These rules are taken from earlier small-screen designs:

- **Actions first.** An article lists its actions (detector, quiz) before the definition, so one
  Down reaches them. Palm OS: frequent commands get one tap.
- **Feature-phone browsing.** A press either focuses the next link in view or moves the text.
  This is how WAP browsers made links reachable with a d-pad.
- **Sticky section heading.** The header always says which section you are in and where it
  sits ("2/5"), the way a TI-83 title line names its current menu.
- **Softkeys at fixed places.** Firmware-style inverted tabs keep the same position for the same
  meaning (TI-83 guidelines), and the detector's answers never move.
- **No wall of text.** After three lines, a paragraph breaks at its next sentence end and
  leaves a 3 px gap, so a long definition reads as short blocks.
- **Details on demand.** The map's side boxes hold a few characters. The full name of the
  focused box appears in the header.
- **Body text at 10 px pitch** in haxrcorp4089, about 5 lines a screen. The firmware's own font
  table gives 11 as default and 9 as minimum for this font. Shorter lines read better than more
  lines (Duchnicky and Kolers, 1983).
- **No scores.** The detector shows the operator's own marks as a number, a bar and a legend,
  never a percentage or a verdict.

## How it is tested

- **The simulator.** `tests/sim` builds the same `app/ui` on the host against the firmware's own
  u8g2 and fonts. [`docs/evidence/parity`](evidence/parity/manifest.json) records a run in
  which the device and the simulator drew identical frames for the same keys.
- **Scenarios.** `tests/sim/scenarios.txt` lists the key paths. Each one asserts the visible
  text: the header and position, focused rows, and softkeys. The scenarios run under ASan and
  UBSan on hermetic packs, including corrupted records and an SD card pulled mid-use.
  `tests/sim/run.sh` fails on any line of `app/ui` that no scenario ran.
- **Hardware.** Every frame in the README gallery is read from the device's screen.
