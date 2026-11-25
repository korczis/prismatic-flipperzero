/* Page: the one view engine of Prismatic Pocket, portable (canvas API + C99, no Furi).
 *
 * A page is a pinned header, a body of styled blocks and an optional softkey bar. The same
 * engine draws documents (articles, results) and lists (menus, options): a list is a page of
 * rows. Design rules (docs/ui.md) live here in one table, so every screen looks alike:
 *
 *   header   12 px: bold title left, position right ("2/5", "12/64"), 1 px rule; in a
 *            document the title becomes the current section once its heading scrolls by
 *   body     FontSecondary, 10 px leading, text 2..123, scrollbar 125..127
 *   heading  full-width inverted bar; Left/Right jump between headings
 *   rows     12 px, icon + label + right-aligned count, selection = inverted rounded box
 *   links    chevron + text; focused = inverted rounded box
 *   softkeys 12 px bar, labels fixed at left / centre / right
 *
 * Navigation (feature-phone browser style): Up/Down move the focus to the next selectable
 * block in view, otherwise scroll one line; Left/Right jump to the previous/next heading (in a
 * list: page up/down); OK opens the focused block; with softkeys, Left/OK/Right press them
 * (hold OK = the long action). Buffers are allocated once; drawing allocates nothing.
 */
#pragma once

#include <gui/canvas.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PageTitle, /* the document's own title: bold, wrapped */
    PageHeading, /* inverted section bar */
    PageMeta, /* small caption line (kind, code, source) with a dotted rule under it */
    PageParagraph,
    PageBullet,
    PageNote, /* quoted: a bar on the left */
    PageLink, /* selectable, opens its target */
    PageRow, /* selectable list row: icon, label, right text */
    PageBig, /* big number + caption (results) */
    PageBar, /* stacked bar with legend: up to 4 "value label" segments, "3 seen,1 no" */
    PageQuote, /* an utterance, framed */
    PageSteps, /* progress cells: text "done/total" */
    PageBadge, /* kind as an inverted pill, then a caption: text "KIND\tcaption" */
    PageChip, /* selectable: icon + short text; consecutive chips share one row */
    PageTile, /* selectable: big icon, label, caption; consecutive tiles form a 2-column grid */
} PageStyle;

typedef enum {
    PageKeyUp,
    PageKeyDown,
    PageKeyLeft,
    PageKeyRight,
    PageKeyOk,
    PageKeyBack,
} PageKey;

typedef enum {
    PagePressShort,
    PagePressLong,
    PagePressRepeat,
} PagePress;

typedef enum {
    PageActNone, /* nothing for the app to do (maybe a redraw) */
    PageActOpen, /* open `target` of the focused block */
    PageActSoftkey, /* softkey `target` (0..2) pressed; `long_press` for a held OK */
    PageActBack,
    PageActHome, /* Back held: straight to the first screen */
} PageActKind;

typedef struct {
    PageActKind kind;
    uint32_t target;
    bool long_press;
} PageAction;

typedef struct Page Page;

Page* page_alloc(void);
void page_free(Page* page);

/* Building: reset, set chrome, add blocks. Text is copied; a full page ends with a note. */
void page_reset(Page* page, const char* title);
void page_set_softkeys(Page* page, const char* left, const char* center, const char* right);
/* Position text shown right in the header, e.g. "3/5"; NULL derives it (sections or rows). */
void page_set_position(Page* page, const char* position);
void page_add(Page* page, PageStyle style, const char* text, uint32_t target);
/* A row: icon (0 = none, see ui_icons.h), label, right-aligned text (may be NULL). */
void page_add_row(Page* page, uint8_t icon, const char* label, const char* right, uint32_t target);
/* A tile of a two-column grid (Home): a 16 px icon, a bold label and a caption under it. The d-pad
 * moves in two dimensions inside the grid; Up/Down past its edge leave it. */
void page_add_tile(
    Page* page,
    uint8_t icon,
    const char* label,
    const char* caption,
    uint32_t target);
/* A one-line explanation of the block just added, shown in a status line at the bottom while that
 * block has the focus (details on demand). */
void page_set_hint(Page* page, const char* hint);
/* A row with a mini bar chart before its count: `value` of `max` (a share of the largest). */
void page_add_row_bar(
    Page* page,
    uint8_t icon,
    const char* label,
    const char* right,
    uint32_t value,
    uint32_t max,
    uint32_t target);
/* Any block with an icon: section headings and chips. */
void page_add_icon(Page* page, PageStyle style, uint8_t icon, const char* text, uint32_t target);
/* An icon (ui icons.h) left of the header title; 0 for none. */
void page_set_title_icon(Page* page, uint8_t icon);
/* After the next layout: scroll to `top` and focus `target` (0 = none). Restores a page on Back. */
void page_restore(Page* page, int16_t top, uint32_t target);
int16_t page_scroll(const Page* page);
uint32_t page_focused_target(const Page* page);
uint16_t page_block_count(const Page* page);

/* Context menu: hold OK on a page without softkeys opens it over the page. Entries open their
 * target like a link; "Contents" (the page's own section headings) is added by the engine
 * whenever the page has two or more sections. */
void page_add_menu(Page* page, const char* label, uint32_t target);

/* Local relationship graph: the page body becomes a map of one centre and its neighbours.
 * Up/Right/Down/Left focus the neighbour on that side; the same direction again shows the next
 * four; the opposite one returns to the centre. OK opens the focused node's target; holding OK
 * on a neighbour opens it with long_press set. `kind` 1 draws a dotted edge (confusable). */
void page_set_graph(Page* page, const char* centre, uint32_t target);
void page_add_node(Page* page, const char* label, uint8_t kind, uint32_t target);

void page_draw(Page* page, Canvas* canvas);
PageAction page_input(Page* page, PageKey key, PagePress press);
/* Call about every 250 ms while idle: a focused row whose label is cut scrolls it by characters
 * after a pause (a feature-phone marquee). Returns true when the page needs a redraw. */
bool page_tick(Page* page);

/* Test and simulator hooks: the visible text, one block line per entry. */
uint16_t page_visible_lines(const Page* page, char out[][64], uint16_t cap);
