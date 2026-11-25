#include "page.h"

#include "fonts.h"
#include "icons.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TEXT_CAP  12288u
#define BLOCK_CAP 220u
#define LINE_CAP  900u
#define TITLE_CAP 48u
#define POS_CAP   12u
#define KEY_CAP   10u
#define MENU_CAP  8u
#define LABEL_CAP 48u
#define NODE_CAP  24u
#define CONTENTS  0xFFFFFFFEu /* the engine's own "Contents" menu entry */

/* Geometry: docs/ui.md explains every number. */
#define SCREEN_W  128
#define SCREEN_H  64
#define HEADER_H  13 /* title baseline 9, rule at y 11, body from y 13 */
#define SOFT_H    12
#define LEFT      2
#define RIGHT     123 /* last text column; the scrollbar is 125..127 */
#define LEADING   11 /* body (helvR08) line pitch */
#define CAPTION_H 10 /* caption (haxrcorp4089) line pitch */
#define CHIP_H    15
#define ROW_H     12
#define HEADING_H 13
#define BIG_H     24
#define BAR_H     21
#define GAP       3

typedef struct {
    uint8_t style;
    uint8_t icon;
    uint16_t off;
    uint16_t len;
    uint16_t right_off;
    uint8_t right_len;
    uint32_t target;
    uint16_t line_first; /* set by layout: the block's first and last line */
    uint16_t line_last;
    uint8_t bar; /* rows: share of the largest value in a list, 0..100 */
} Block;

typedef struct {
    uint16_t block;
    uint16_t off;
    uint8_t len;
    uint8_t height;
    uint8_t flags; /* LINE_FIRST, LINE_LAST of its block */
} Line;

#define LINE_FIRST 1u
#define LINE_LAST  2u
#define LINE_BODY  4u /* content, as opposed to a gap */

struct Page {
    char* text;
    uint16_t text_len;
    Block* blocks;
    uint16_t blocks_n;
    Line* lines;
    uint16_t lines_n;
    bool dirty;
    bool truncated;
    bool list; /* every selectable block is a row: lists wrap and page by rows */
    int16_t top;
    int16_t focus; /* block index of the focused selectable block, or -1 */
    uint32_t want_target; /* focus to restore after layout */
    bool want;
    int16_t want_top;
    uint8_t title_icon;
    char title[TITLE_CAP];
    char position[POS_CAP];
    bool position_set;
    char softkey[3][KEY_CAP];
    /* context menu and the outline it can open */
    char menu_label[MENU_CAP][LABEL_CAP];
    uint32_t menu_target[MENU_CAP];
    uint8_t menu_n;
    uint8_t overlay; /* OverlayNone, OverlayMenu, OverlayOutline */
    int16_t overlay_sel;
    /* local graph: node 0 is the centre */
    bool graph;
    char node_label[NODE_CAP + 1][LABEL_CAP];
    uint32_t node_target[NODE_CAP + 1];
    uint8_t node_kind[NODE_CAP + 1];
    uint8_t nodes_n; /* neighbours, without the centre */
    uint8_t node_window; /* first neighbour shown */
    int8_t node_focus; /* 0 centre, 1..4 top, right, bottom, left */
};

enum {
    OverlayNone,
    OverlayMenu,
    OverlayOutline,
};

/* ------------------------------------------------------------------ building */

Page* page_alloc(void) {
    Page* p = calloc(1, sizeof(Page));
    p->text = malloc(TEXT_CAP);
    p->blocks = malloc(sizeof(Block) * BLOCK_CAP);
    p->lines = malloc(sizeof(Line) * LINE_CAP);
    page_reset(p, "");
    return p;
}

void page_free(Page* p) {
    free(p->text);
    free(p->blocks);
    free(p->lines);
    free(p);
}

static void copy_str(char* dst, const char* src, size_t cap) {
    size_t n = src ? strlen(src) : 0;
    if(n >= cap) n = cap - 1;
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

void page_reset(Page* p, const char* title) {
    p->text_len = 0;
    p->blocks_n = 0;
    p->lines_n = 0;
    p->dirty = true;
    p->truncated = false;
    p->list = false;
    p->top = 0;
    p->focus = -1;
    p->want = false;
    p->want_top = 0;
    p->title_icon = 0;
    p->position_set = false;
    p->position[0] = '\0';
    copy_str(p->title, title, TITLE_CAP);
    memset(p->softkey, 0, sizeof(p->softkey));
    p->menu_n = 0;
    p->overlay = OverlayNone;
    p->graph = false;
    p->nodes_n = 0;
    p->node_window = 0;
    p->node_focus = 0;
}

void page_add_menu(Page* p, const char* label, uint32_t target) {
    if(p->menu_n >= MENU_CAP) return;
    copy_str(p->menu_label[p->menu_n], label, LABEL_CAP);
    p->menu_target[p->menu_n++] = target;
}

void page_set_graph(Page* p, const char* centre, uint32_t target) {
    p->graph = true;
    copy_str(p->node_label[0], centre, LABEL_CAP);
    p->node_target[0] = target;
    p->node_kind[0] = 0;
}

void page_add_node(Page* p, const char* label, uint8_t kind, uint32_t target) {
    if(p->nodes_n >= NODE_CAP) return;
    uint8_t i = ++p->nodes_n;
    copy_str(p->node_label[i], label, LABEL_CAP);
    p->node_target[i] = target;
    p->node_kind[i] = kind;
}

void page_set_softkeys(Page* p, const char* left, const char* center, const char* right) {
    copy_str(p->softkey[0], left, KEY_CAP);
    copy_str(p->softkey[1], center, KEY_CAP);
    copy_str(p->softkey[2], right, KEY_CAP);
    p->dirty = true;
}

void page_set_title_icon(Page* p, uint8_t icon) {
    p->title_icon = icon;
}

void page_restore(Page* p, int16_t top, uint32_t target) {
    p->want_top = top;
    p->want = true;
    p->want_target = target;
}

int16_t page_scroll(const Page* p) {
    return p->top;
}

void page_set_position(Page* p, const char* position) {
    p->position_set = position != NULL;
    copy_str(p->position, position, POS_CAP);
}

/* Appends text; returns its offset, or -1 when the page is full (and ends it with a note). */
static int32_t put_text(Page* p, const char* text, size_t n) {
    if(p->truncated) return -1;
    if((uint32_t)p->blocks_n + 2 > BLOCK_CAP || p->text_len + n + 40 > TEXT_CAP) {
        static const char note[] = "(more in the pack than fits on one page)";
        p->truncated = true;
        memcpy(p->text + p->text_len, note, sizeof(note) - 1);
        p->blocks[p->blocks_n++] =
            (Block){PageNote, 0, p->text_len, (uint16_t)(sizeof(note) - 1), 0, 0, 0, 0, 0, 0};
        p->text_len += (uint16_t)(sizeof(note) - 1);
        return -1;
    }
    int32_t at = p->text_len;
    memcpy(p->text + p->text_len, text, n);
    p->text_len += (uint16_t)n;
    return at;
}

void page_add(Page* p, PageStyle style, const char* text, uint32_t target) {
    size_t n = strlen(text);
    while(n && text[n - 1] == ' ')
        n--;
    if(n == 0) return;
    int32_t at = put_text(p, text, n);
    if(at < 0) return;
    p->blocks[p->blocks_n++] =
        (Block){(uint8_t)style, 0, (uint16_t)at, (uint16_t)n, 0, 0, target, 0, 0, 0};
    p->dirty = true;
}

void page_add_row(Page* p, uint8_t icon, const char* label, const char* right, uint32_t target) {
    size_t n = strlen(label), rn = right ? strlen(right) : 0;
    if(rn > 255) rn = 255;
    int32_t at = put_text(p, label, n);
    if(at < 0) return;
    int32_t rat = rn ? put_text(p, right, rn) : 0;
    if(rat < 0) return;
    p->blocks[p->blocks_n++] = (Block){
        PageRow, icon, (uint16_t)at, (uint16_t)n, (uint16_t)rat, (uint8_t)rn, target, 0, 0, 0};
    p->list = true;
    p->dirty = true;
}

void page_add_row_bar(
    Page* p,
    uint8_t icon,
    const char* label,
    const char* right,
    uint32_t value,
    uint32_t max,
    uint32_t target) {
    page_add_row(p, icon, label, right, target);
    if(p->blocks_n && p->blocks[p->blocks_n - 1].style == PageRow && max) {
        /* a square-root scale, so small entries stay visible next to the largest */
        uint32_t ratio = (uint32_t)((uint64_t)value * 10000u / max), share = 0;
        while((share + 1) * (share + 1) <= ratio)
            share++;
        p->blocks[p->blocks_n - 1].bar = (uint8_t)(share > 100 ? 100 : share ? share : 1);
    }
}

void page_add_icon(Page* p, PageStyle style, uint8_t icon, const char* text, uint32_t target) {
    uint16_t before = p->blocks_n;
    page_add(p, style, text, target);
    if(p->blocks_n > before) p->blocks[p->blocks_n - 1].icon = icon;
}

static bool selectable(const Block* b) {
    return b->style == PageLink || b->style == PageRow || b->style == PageChip;
}

uint16_t page_block_count(const Page* p) {
    return p->blocks_n;
}

uint32_t page_focused_target(const Page* p) {
    return p->focus >= 0 ? p->blocks[p->focus].target : 0;
}

/* ------------------------------------------------------------------ layout */

static bool has_softkeys(const Page* p) {
    return p->softkey[0][0] || p->softkey[1][0] || p->softkey[2][0];
}

static int body_bottom(const Page* p) {
    return has_softkeys(p) ? SCREEN_H - SOFT_H : SCREEN_H;
}

static int body_height(const Page* p) {
    return body_bottom(p) - HEADER_H;
}

/* Three roles, one family: helvB08 for titles and headings, helvR08 for reading, haxrcorp4089
 * for captions (meta, notes, counts, legends). */
typedef enum {
    RoleTitle,
    RoleBody,
    RoleCaption,
} Role;

static Role role_of(uint8_t style) {
    switch(style) {
    case PageTitle:
    case PageHeading:
        return RoleTitle;
    case PageMeta:
    case PageNote:
    case PageBadge:
    case PageChip:
        return RoleCaption;
    default:
        return RoleBody;
    }
}

static void set_role(Canvas* canvas, Role role) {
    if(role == RoleTitle)
        canvas_set_font(canvas, FontPrimary);
    else if(role == RoleBody)
        canvas_set_custom_u8g2_font(canvas, pocket_font_body);
    else
        canvas_set_font(canvas, FontSecondary);
}

static void set_font_of(Canvas* canvas, uint8_t style) {
    set_role(canvas, role_of(style));
}

static int indent_of(uint8_t style) {
    switch(style) {
    case PageBullet:
        return LEFT + 6;
    case PageLink:
        return LEFT + 8;
    case PageNote:
        return LEFT + 5;
    case PageQuote:
        return LEFT + 4;
    case PageHeading:
        return LEFT + 2;
    default:
        return LEFT;
    }
}

static int right_of(uint8_t style) {
    return style == PageQuote ? RIGHT - 4 : style == PageHeading ? RIGHT - 2 : RIGHT;
}

static void push_line(Page* p, uint16_t block, uint16_t off, uint16_t len, uint8_t h, uint8_t f) {
    if(p->lines_n >= LINE_CAP) return;
    if(f & LINE_FIRST) p->blocks[block].line_first = p->lines_n;
    if(f & LINE_LAST) p->blocks[block].line_last = p->lines_n;
    Line* l = &p->lines[p->lines_n++];
    l->block = block;
    l->off = off;
    l->len = (uint8_t)(len > 255 ? 255 : len);
    l->height = h;
    l->flags = f;
}

static void push_gap(Page* p, uint16_t block, uint8_t h) {
    push_line(p, block, 0, 0, h, 0);
}

static bool is_gap(const Line* l) {
    return !(l->flags & LINE_BODY);
}

/* Greedy word wrap with the fonts' real glyph widths; a word longer than a line is broken. */
static void wrap(Page* p, Canvas* canvas, uint16_t b, uint8_t height) {
    const Block* blk = &p->blocks[b];
    const char* s = p->text + blk->off;
    int avail = right_of(blk->style) - indent_of(blk->style);
    uint16_t start = 0, last_space = 0, i = 0, first_line = p->lines_n, run = 0;
    int width = 0;
    bool have_space = false;
    set_font_of(canvas, blk->style);
    while(i < blk->len) {
        int w = (int)canvas_glyph_width(canvas, (uint8_t)s[i]);
        if(width + w > avail && i > start) {
            uint16_t cut = have_space ? last_space : i;
            push_line(p, b, blk->off + start, cut - start, height, LINE_BODY);
            run++;
            start = have_space ? (uint16_t)(last_space + 1) : i;
            i = start;
            width = 0;
            have_space = false;
            continue;
        }
        if(s[i] == ' ') {
            last_space = i;
            have_space = true;
        }
        width += w;
        i++;
        /* a paragraph three lines long breaks at its next sentence end: no wall of text */
        if(blk->style == PageParagraph && run >= 3 && s[i - 1] == '.' && i < blk->len &&
           s[i] == ' ') {
            push_line(p, b, blk->off + start, i - start, height, LINE_BODY);
            push_gap(p, b, GAP);
            start = (uint16_t)(i + 1);
            i = start;
            width = 0;
            have_space = false;
            run = 0;
        }
    }
    if(i > start || p->lines_n == first_line) {
        push_line(p, b, blk->off + start, i - start, height, LINE_BODY);
    }
    if(first_line < p->lines_n) {
        p->lines[first_line].flags |= LINE_FIRST;
        p->lines[p->lines_n - 1].flags |= LINE_LAST;
        p->blocks[b].line_first = first_line;
        p->blocks[b].line_last = (uint16_t)(p->lines_n - 1);
    }
}

static int16_t first_line_of(const Page* p, int16_t b) {
    return (int16_t)p->blocks[b].line_first;
}

static int16_t last_line_of(const Page* p, int16_t b) {
    return (int16_t)p->blocks[b].line_last;
}

static int16_t max_top(const Page* p) {
    int h = 0, vh = body_height(p);
    for(int16_t i = (int16_t)p->lines_n - 1; i >= 0; i--) {
        h += p->lines[i].height;
        if(h > vh) return (int16_t)(i + 1);
    }
    return 0;
}

/* Last line index fully inside the body when the first visible line is `top`. */
static int16_t last_visible(const Page* p, int16_t top) {
    int h = 0, vh = body_height(p);
    int16_t i = top;
    for(; i < (int16_t)p->lines_n; i++) {
        h += p->lines[i].height;
        if(h > vh) break;
    }
    return (int16_t)(i - 1);
}

static void reveal(Page* p, int16_t b) {
    int16_t first = first_line_of(p, b), last = last_line_of(p, b), mt = max_top(p);
    if(first < p->top) p->top = first;
    while(last > last_visible(p, p->top) && p->top < first && p->top < mt)
        p->top++;
}

static int chip_width(const Page* p, Canvas* canvas, uint16_t b) {
    char buf[32];
    const Block* blk = &p->blocks[b];
    size_t n = blk->len < sizeof(buf) - 1 ? blk->len : sizeof(buf) - 1;
    memcpy(buf, p->text + blk->off, n);
    buf[n] = '\0';
    set_role(canvas, RoleCaption);
    return canvas_string_width(canvas, buf) + (blk->icon ? 17 : 8);
}

/* Chips flow left to right on one row (a page holds a few short ones): the first takes the
 * row's height, the rest sit beside it; a line's `off` holds its x. */
static void layout_chip(Page* p, Canvas* canvas, uint16_t b, bool next_same) {
    bool cont = b > 0 && p->blocks[b - 1].style == PageChip;
    int x = LEFT;
    if(cont) x = p->lines[p->lines_n - 1].off + chip_width(p, canvas, (uint16_t)(b - 1)) + 3;
    push_line(p, b, 0, 0, cont ? 0 : CHIP_H, LINE_FIRST | LINE_LAST | LINE_BODY);
    p->lines[p->lines_n - 1].off = (uint16_t)x;
    if(!next_same) push_gap(p, b, 2);
}

static void layout(Page* p, Canvas* canvas) {
    p->lines_n = 0;
    for(uint16_t b = 0; b < p->blocks_n; b++) {
        uint8_t st = p->blocks[b].style;
        bool next_same = b + 1 < p->blocks_n && p->blocks[b + 1].style == st;
        switch(st) {
        case PageRow:
            push_line(p, b, 0, 0, ROW_H, LINE_FIRST | LINE_LAST | LINE_BODY);
            break;
        case PageBig:
            push_line(p, b, 0, 0, BIG_H, LINE_FIRST | LINE_LAST | LINE_BODY);
            break;
        case PageSteps:
            push_line(p, b, 0, 0, 8, LINE_FIRST | LINE_LAST | LINE_BODY);
            break;
        case PageBadge:
            push_line(p, b, 0, 0, 12, LINE_FIRST | LINE_LAST | LINE_BODY);
            break;
        case PageChip:
            layout_chip(p, canvas, b, next_same);
            break;
        case PageBar:
            push_line(p, b, 0, 0, BAR_H, LINE_FIRST | LINE_LAST | LINE_BODY);
            push_gap(p, b, GAP);
            break;
        case PageHeading:
            if(b > 0) push_gap(p, b, 2);
            wrap(p, canvas, b, HEADING_H);
            break;
        case PageTitle:
            wrap(p, canvas, b, 11);
            push_gap(p, b, 1);
            break;
        case PageQuote:
            push_gap(p, b, 2); /* the frame's top edge */
            wrap(p, canvas, b, LEADING);
            push_gap(p, b, GAP + 2); /* bottom edge, then the gap */
            break;
        default:
            wrap(p, canvas, b, role_of(st) == RoleCaption ? CAPTION_H : LEADING);
            if(!(next_same && (st == PageBullet || st == PageLink))) push_gap(p, b, GAP);
            break;
        }
    }
    p->dirty = false;
    if(p->want) {
        p->want = false;
        p->top = p->want_top;
        if(p->top > max_top(p)) p->top = max_top(p);
        for(uint16_t b = 0; b < p->blocks_n; b++) {
            if(selectable(&p->blocks[b]) && p->blocks[b].target == p->want_target) {
                p->focus = (int16_t)b;
                reveal(p, (int16_t)b);
                break;
            }
        }
    }
    if(p->top > max_top(p)) p->top = max_top(p);
    if(p->focus < 0 && p->list) {
        for(uint16_t b = 0; b < p->blocks_n; b++) {
            if(selectable(&p->blocks[b])) {
                p->focus = (int16_t)b;
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ drawing */

static void fit(Canvas* canvas, const char* src, size_t n, int width, char* out, size_t cap) {
    if(n >= cap) n = cap - 1;
    memcpy(out, src, n);
    out[n] = '\0';
    if(canvas_string_width(canvas, out) <= width) return;
    int dots = canvas_string_width(canvas, "...");
    while(n > 0 && canvas_string_width(canvas, out) + dots > width)
        out[--n] = '\0';
    while(n > 0 && out[n - 1] == ' ')
        out[--n] = '\0';
    memcpy(out + n, "...", 4);
}

static void draw_icon(Canvas* canvas, uint8_t icon, int x, int y) {
    if(icon == 0 || icon >= UiIconCount) return;
    for(int r = 0; r < UI_ICON_H; r++) {
        uint8_t bits = ui_icon_rows[icon][r];
        for(int c = 0; c < UI_ICON_W; c++) {
            if(bits & (1u << c)) canvas_draw_dot(canvas, x + c, y + r);
        }
    }
}

/* 50 % dither for the "unknown" share of a bar: pixels where x + y is even. */
static void draw_dither(Canvas* canvas, int x, int y, int w, int h) {
    for(int yy = y; yy < y + h; yy++) {
        for(int xx = x + ((x + yy) & 1); xx < x + w; xx += 2)
            canvas_draw_dot(canvas, xx, yy);
    }
}

/* One share's fill: 0 solid, 1 empty, 2 dots, 3 hatch; used by the bar and its legend. */
static void draw_fill(Canvas* canvas, int kind, int x, int y, int w, int h) {
    if(kind == 0) canvas_draw_box(canvas, x, y, (size_t)w, (size_t)h);
    if(kind == 2) draw_dither(canvas, x, y, w, h);
    if(kind == 3) {
        for(int xx = x; xx < x + w; xx += 3)
            canvas_draw_line(canvas, xx, y + h - 1, xx + 1, y);
    }
}

/* A bar's spec: up to four "value label" segments separated by commas, e.g. "3 seen,1 no". */
typedef struct {
    int v[4], total, k;
    const char* name[4];
    size_t name_len[4];
} BarSpec;

static void bar_parse(const char* spec, size_t n, BarSpec* b) {
    memset(b, 0, sizeof(*b));
    size_t i = 0;
    while(i < n && b->k < 4) {
        while(i < n && spec[i] >= '0' && spec[i] <= '9')
            b->v[b->k] = b->v[b->k] * 10 + (spec[i++] - '0');
        while(i < n && spec[i] == ' ')
            i++;
        b->name[b->k] = spec + i;
        while(i < n && spec[i] != ',')
            i++;
        b->name_len[b->k] = (size_t)(spec + i - b->name[b->k]);
        b->total += b->v[b->k++];
        i++; /* the comma */
    }
}

static void draw_bar(Canvas* canvas, const BarSpec* b, int y) {
    int x = LEFT, w = RIGHT - LEFT + 1, h = 8;
    canvas_draw_rframe(canvas, x, y + 1, (size_t)w, (size_t)h, 1);
    if(b->total == 0) return;
    int at = x + 1, inner = w - 2;
    for(int i = 0; i < b->k; i++) {
        int seg = (b->v[i] * inner + b->total / 2) / b->total;
        if(i == b->k - 1) seg = x + 1 + inner - at;
        if(seg <= 0) continue;
        draw_fill(canvas, i, at, y + 2, seg, h - 2);
        at += seg;
        if(at < x + w - 1) canvas_draw_line(canvas, at, y + 2, at, y + h - 1);
    }
}

/* One swatch per segment with its value and label, in the segment's fill. */
static void draw_legend(Canvas* canvas, const BarSpec* b, int y) {
    int x = LEFT;
    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < b->k; i++) {
        char label[24];
        size_t len = b->name_len[i] < 14 ? b->name_len[i] : 14;
        int at = snprintf(label, sizeof(label), "%d ", b->v[i]);
        memcpy(label + at, b->name[i], len);
        label[(size_t)at + len] = '\0';
        canvas_draw_frame(canvas, x, y + 2, 6, 6);
        draw_fill(canvas, i, x + 1, y + 3, 4, 4);
        canvas_draw_str(canvas, x + 8, y + 8, label);
        x += 8 + canvas_string_width(canvas, label) + 6;
    }
}

static void draw_bar_block(Canvas* canvas, const char* text, size_t n, int y) {
    BarSpec spec;
    bar_parse(text, n, &spec);
    draw_bar(canvas, &spec, y);
    draw_legend(canvas, &spec, y + 10);
}

static void draw_row(Canvas* canvas, const Page* p, const Block* blk, int y, bool focused) {
    char buf[256];
    int x = LEFT + 1;
    if(focused) {
        canvas_draw_rbox(canvas, 0, y, RIGHT + 1, ROW_H, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    if(blk->icon) {
        draw_icon(canvas, blk->icon, x + 1, y + 3);
        x += 12;
    }
    int rw = 0;
    set_role(canvas, RoleCaption);
    if(blk->right_len) {
        memcpy(buf, p->text + blk->right_off, blk->right_len);
        buf[blk->right_len] = '\0';
        rw = canvas_string_width(canvas, buf);
        canvas_draw_str(canvas, RIGHT - 2 - rw, y + 9, buf);
        rw += 4;
    }
    if(blk->bar) {
        /* a mini bar chart: a dotted 18 px track, filled by the row's share of the largest */
        int bx = RIGHT - 2 - rw - 14, fill = blk->bar * 14 / 100;
        for(int dx = 0; dx < 14; dx += 2)
            canvas_draw_dot(canvas, bx + dx, y + 6);
        canvas_draw_box(canvas, bx, y + 4, (size_t)(fill ? fill : 1), 5);
        rw += 18;
    }
    set_role(canvas, RoleBody);
    fit(canvas, p->text + blk->off, blk->len, RIGHT - 2 - rw - x, buf, sizeof(buf));
    canvas_draw_str(canvas, x, y + 9, buf);
    canvas_set_color(canvas, ColorBlack);
}

/* "N/M caption": the number in the big font, the caption beside it. */
static void draw_big(Canvas* canvas, const Page* p, const Block* blk, int y) {
    char buf[256], num[16];
    const char* s = p->text + blk->off;
    size_t n = blk->len, k = 0;
    while(k < n && s[k] != ' ')
        k++;
    size_t kn = k < sizeof(num) ? k : sizeof(num) - 1;
    memcpy(num, s, kn);
    num[kn] = '\0';
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, LEFT, y + 18, num);
    int x = LEFT + canvas_string_width(canvas, num) + 5;
    canvas_set_font(canvas, FontSecondary);
    fit(canvas, s + (k < n ? k + 1 : n), k < n ? n - k - 1 : 0, RIGHT - x, buf, sizeof(buf));
    canvas_draw_str(canvas, x, y + 17, buf);
}

/* "done/total": one cell per step, done filled, the current one framed twice. */
static void draw_steps(Canvas* canvas, const char* spec, size_t n, int y) {
    int done = 0, total = 0, *v = &done;
    for(size_t i = 0; i < n; i++) {
        if(spec[i] == '/')
            v = &total;
        else if(spec[i] >= '0' && spec[i] <= '9')
            *v = *v * 10 + (spec[i] - '0');
    }
    if(total <= 0) return;
    int gap = 2, w = (RIGHT - LEFT + 1 - gap * (total - 1)) / total;
    if(w > 14) w = 14;
    if(w < 2) w = 2;
    for(int k = 0; k < total; k++) {
        int x = LEFT + k * (w + gap);
        if(x + w > RIGHT + 1) break;
        if(k < done)
            canvas_draw_box(canvas, x, y + 2, (size_t)w, 4);
        else
            canvas_draw_frame(canvas, x, y + 2, (size_t)w, 4);
        if(k == done) canvas_draw_frame(canvas, x - 1, y + 1, (size_t)(w + 2), 6);
    }
}

/* "KIND\tcaption": the kind in capitals inside an inverted pill, the caption after it. */
static void draw_badge(Canvas* canvas, const Page* p, const Block* blk, int y) {
    char kind[24], caption[64];
    const char* s = p->text + blk->off;
    size_t n = blk->len, k = 0;
    while(k < n && s[k] != '\t')
        k++;
    size_t kn = k < sizeof(kind) - 1 ? k : sizeof(kind) - 1;
    for(size_t i = 0; i < kn; i++)
        kind[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    kind[kn] = '\0';
    set_role(canvas, RoleCaption);
    /* small capitals read better tracked out by one pixel */
    int w = 5;
    for(size_t i = 0; i < kn; i++)
        w += (int)canvas_glyph_width(canvas, (uint8_t)kind[i]) + 1;
    canvas_draw_rbox(canvas, LEFT, y, (size_t)w, 11, 2);
    canvas_set_color(canvas, ColorWhite);
    for(size_t i = 0, x = LEFT + 3; i < kn; i++) {
        char one[2] = {kind[i], '\0'};
        canvas_draw_str(canvas, (int32_t)x, y + 9, one);
        x += canvas_glyph_width(canvas, (uint8_t)kind[i]) + 1;
    }
    canvas_set_color(canvas, ColorBlack);
    if(k < n) {
        fit(canvas, s + k + 1, n - k - 1, RIGHT - LEFT - w - 4, caption, sizeof(caption));
        canvas_draw_str(canvas, LEFT + w + 4, y + 9, caption);
    }
}

/* A chip: icon and a short text in a rounded box; focused = inverted. */
static void
    draw_chip(Canvas* canvas, const Page* p, const Block* blk, int x, int y, bool focused) {
    char buf[32];
    size_t n = blk->len < sizeof(buf) - 1 ? blk->len : sizeof(buf) - 1;
    memcpy(buf, p->text + blk->off, n);
    buf[n] = '\0';
    set_role(canvas, RoleCaption);
    int w = canvas_string_width(canvas, buf) + (blk->icon ? 17 : 8);
    if(focused) {
        canvas_draw_rbox(canvas, x, y + 1, (size_t)w, 12, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y + 1, (size_t)w, 12, 3);
    }
    int tx = x + 4;
    if(blk->icon) {
        draw_icon(canvas, blk->icon, x + 4, y + 4);
        tx += 9;
    }
    canvas_draw_str(canvas, tx, y + 10, buf);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_line(Canvas* canvas, const Page* p, const Line* l, int y) {
    const Block* blk = &p->blocks[l->block];
    char buf[256];
    bool focused = p->focus == (int16_t)l->block;
    if(is_gap(l)) {
        /* gaps draw nothing but a quote's frame edges */
        if(blk->style == PageQuote && l->height == 2) {
            canvas_draw_line(canvas, LEFT + 2, y + 1, RIGHT - 2, y + 1);
            canvas_draw_dot(canvas, LEFT + 1, y + 2);
            canvas_draw_dot(canvas, RIGHT - 1, y + 2);
        } else if(blk->style == PageQuote) {
            canvas_draw_dot(canvas, LEFT + 1, y - 1);
            canvas_draw_dot(canvas, RIGHT - 1, y - 1);
            canvas_draw_line(canvas, LEFT + 2, y, RIGHT - 2, y);
        }
        return;
    }
    switch(blk->style) {
    case PageRow:
        draw_row(canvas, p, blk, y, focused);
        return;
    case PageBig:
        draw_big(canvas, p, blk, y);
        return;
    case PageBar:
        draw_bar_block(canvas, p->text + blk->off, blk->len, y);
        return;
    case PageSteps:
        draw_steps(canvas, p->text + blk->off, blk->len, y);
        return;
    case PageBadge:
        draw_badge(canvas, p, blk, y);
        return;
    case PageChip:
        draw_chip(canvas, p, blk, l->off, y - (l->height ? 0 : CHIP_H), focused);
        return;
    default:
        break;
    }
    memcpy(buf, p->text + l->off, l->len);
    buf[l->len] = '\0';
    set_font_of(canvas, blk->style);
    int x = indent_of(blk->style), base = y + (role_of(blk->style) == RoleBody ? 9 : 8);
    switch(blk->style) {
    case PageHeading:
        canvas_draw_box(canvas, 0, y + 1, RIGHT + 1, HEADING_H - 2);
        canvas_set_color(canvas, ColorWhite);
        if(blk->icon) {
            if(l->flags & LINE_FIRST) draw_icon(canvas, blk->icon, LEFT + 1, y + 4);
            x += 10;
        }
        canvas_draw_str(canvas, x, y + 10, buf);
        canvas_set_color(canvas, ColorBlack);
        break;
    case PageTitle:
        canvas_draw_str(canvas, x, base + 1, buf);
        break;
    case PageMeta:
        canvas_draw_str(canvas, x, base, buf);
        if(l->flags & LINE_LAST) {
            for(int dx = LEFT; dx <= RIGHT; dx += 2)
                canvas_draw_dot(canvas, dx, y + l->height);
        }
        break;
    case PageBullet:
        if(l->flags & LINE_FIRST) canvas_draw_box(canvas, LEFT + 1, base - 5, 3, 3);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageNote:
        canvas_draw_line(canvas, LEFT, y, LEFT, y + l->height - 1);
        canvas_draw_line(canvas, LEFT + 1, y, LEFT + 1, y + l->height - 1);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageQuote:
        canvas_draw_line(canvas, LEFT, y, LEFT, y + l->height - 1);
        canvas_draw_line(canvas, RIGHT, y, RIGHT, y + l->height - 1);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageLink:
        if(focused) {
            canvas_draw_rbox(canvas, 0, y - 1, RIGHT + 1, l->height + 1, 2);
            canvas_set_color(canvas, ColorWhite);
        }
        if(l->flags & LINE_FIRST) {
            int ay = base - 3;
            canvas_draw_line(canvas, LEFT + 2, ay - 2, LEFT + 4, ay);
            canvas_draw_line(canvas, LEFT + 4, ay, LEFT + 2, ay + 2);
            canvas_draw_line(canvas, LEFT + 3, ay - 2, LEFT + 5, ay);
            canvas_draw_line(canvas, LEFT + 5, ay, LEFT + 3, ay + 2);
        }
        canvas_draw_str(canvas, x, base, buf);
        canvas_set_color(canvas, ColorBlack);
        break;
    default:
        canvas_draw_str(canvas, x, base, buf);
        break;
    }
}

/* The heading whose section holds the first visible line, as a block index, or -1. */
static int16_t current_heading(const Page* p, uint16_t* index, uint16_t* count) {
    int16_t at = -1;
    *index = 0;
    *count = 0;
    for(uint16_t i = 0; i < p->lines_n; i++) {
        const Line* l = &p->lines[i];
        if(p->blocks[l->block].style != PageHeading || !(l->flags & LINE_FIRST)) continue;
        (*count)++;
        if((int16_t)i < p->top) {
            at = (int16_t)l->block;
            *index = *count;
        }
    }
    return at;
}

static int node_at(const Page* p, int side);

/* The header's title (the current section once its heading scrolled by) and position. */
static int16_t header_of(const Page* p, const char** t, size_t* tn, char* pos) {
    uint16_t index, count;
    int16_t heading = current_heading(p, &index, &count);
    *t = p->title;
    *tn = strlen(p->title);
    if(p->graph && p->node_focus > 0) {
        /* the map's side boxes are small: the focused one's full name goes in the header */
        *t = p->node_label[node_at(p, p->node_focus)];
        *tn = strlen(*t);
    }
    if(heading >= 0) {
        *t = p->text + p->blocks[heading].off;
        *tn = p->blocks[heading].len;
    }
    pos[0] = '\0';
    if(p->graph && p->nodes_n > 4) {
        unsigned windows = (p->nodes_n + 3u) / 4u, at = p->node_window / 4u + 1u;
        pos[0] = (char)('0' + at % 10);
        pos[1] = '/';
        pos[2] = (char)('0' + windows % 10);
        pos[3] = '\0';
        return heading;
    }
    if(p->position_set) {
        copy_str(pos, p->position, POS_CAP);
    } else if(p->list && p->focus >= 0) {
        uint16_t k = 0, n = 0;
        for(uint16_t b = 0; b < p->blocks_n; b++) {
            if(!selectable(&p->blocks[b])) continue;
            n++;
            if((int16_t)b == p->focus) k = n;
        }
        if(n > 4) {
            int len = 0;
            char* o = pos;
            uint16_t vals[2] = {k, n};
            for(int v = 0; v < 2; v++) {
                char digits[6];
                int d = 0;
                uint16_t x = vals[v];
                do {
                    digits[d++] = (char)('0' + x % 10);
                    x /= 10;
                } while(x);
                while(d)
                    o[len++] = digits[--d];
                if(v == 0) o[len++] = '/';
            }
            o[len] = '\0';
        }
    } else if(count && index) {
        pos[0] = (char)('0' + (index % 10));
        pos[1] = '/';
        pos[2] = (char)('0' + (count % 10));
        pos[3] = '\0';
        if(count >= 10) copy_str(pos, "", POS_CAP);
    }
    return heading;
}

static void draw_header(Canvas* canvas, const Page* p) {
    char pos[POS_CAP], title[TITLE_CAP];
    const char* t;
    size_t tn;
    int16_t heading = header_of(p, &t, &tn, pos);
    canvas_set_font(canvas, FontSecondary);
    int pw = pos[0] ? canvas_string_width(canvas, pos) : 0;
    if(pw) canvas_draw_str(canvas, SCREEN_W - 1 - pw, 9, pos);
    int tx = LEFT;
    uint8_t icon = heading >= 0 ? p->blocks[heading].icon : p->graph ? 0 : p->title_icon;
    if(icon) {
        /* the section's icon travels into the header with its heading */
        draw_icon(canvas, icon, LEFT, 2);
        tx += 11;
    }
    canvas_set_font(canvas, FontPrimary);
    fit(canvas, t, tn, SCREEN_W - 2 - tx - (pw ? pw + 4 : 0), title, sizeof(title));
    canvas_draw_str(canvas, tx, 9, title);
    canvas_draw_line(canvas, 0, 11, SCREEN_W - 1, 11);
}

static void draw_scrollbar(Canvas* canvas, const Page* p) {
    int16_t mt = max_top(p);
    if(mt <= 0) return;
    int top = HEADER_H, h = body_height(p) - 1;
    for(int y = top; y < top + h; y += 2)
        canvas_draw_dot(canvas, SCREEN_W - 2, y);
    int thumb = h * body_height(p) / (body_height(p) + mt * LEADING);
    if(thumb < 4) thumb = 4;
    int y = top + (h - thumb) * p->top / mt;
    canvas_draw_box(canvas, SCREEN_W - 3, y, 3, (size_t)thumb);
}

/* A softkey tab in the firmware's style: inverted, rounded on top, an arrow drawn by lines. */
static void draw_softkey(Canvas* canvas, int slot, const char* label) {
    if(!label[0]) return;
    canvas_set_font(canvas, FontSecondary);
    int tw = canvas_string_width(canvas, label), w = tw + (slot == 1 ? 14 : 12), h = SOFT_H - 1;
    int x = slot == 0 ? 0 : slot == 2 ? SCREEN_W - w : (SCREEN_W - w) / 2, y = SCREEN_H - h;
    canvas_draw_rbox(canvas, x, y, (size_t)w, (size_t)h, 3);
    canvas_draw_box(canvas, x, y + 3, (size_t)w, (size_t)(h - 3));
    canvas_set_color(canvas, ColorWhite);
    int cy = y + h / 2;
    if(slot == 0) {
        canvas_draw_line(canvas, x + 6, cy - 3, x + 3, cy);
        canvas_draw_line(canvas, x + 3, cy, x + 6, cy + 3);
        canvas_draw_line(canvas, x + 6, cy - 3, x + 6, cy + 3);
        canvas_draw_line(canvas, x + 5, cy - 2, x + 5, cy + 2);
        canvas_draw_str(canvas, x + 9, y + 9, label);
    } else if(slot == 2) {
        canvas_draw_str(canvas, x + 3, y + 9, label);
        int ax = x + w - 7;
        canvas_draw_line(canvas, ax, cy - 3, ax + 3, cy);
        canvas_draw_line(canvas, ax + 3, cy, ax, cy + 3);
        canvas_draw_line(canvas, ax, cy - 3, ax, cy + 3);
        canvas_draw_line(canvas, ax + 1, cy - 2, ax + 1, cy + 2);
    } else {
        canvas_draw_disc(canvas, x + 6, cy, 2);
        canvas_draw_str(canvas, x + 11, y + 9, label);
    }
    canvas_set_color(canvas, ColorBlack);
}

/* ------------------------------------------------------------------ overlays and graph */

/* Line indexes of the page's section headings, in order. */
static uint8_t outline(const Page* p, int16_t* at, uint8_t cap) {
    uint8_t n = 0;
    for(uint16_t i = 0; i < p->lines_n && n < cap; i++) {
        const Line* l = &p->lines[i];
        if(p->blocks[l->block].style == PageHeading && (l->flags & LINE_FIRST))
            at[n++] = (int16_t)i;
    }
    return n;
}

static uint8_t menu_count(const Page* p) {
    int16_t at[2];
    return (uint8_t)(p->menu_n + (outline(p, at, 2) >= 2 ? 1 : 0));
}

/* Entry i of the context menu: "Contents" first when the page has sections. */
static const char* menu_entry(const Page* p, uint8_t i, uint32_t* target) {
    int16_t at[2];
    if(outline(p, at, 2) >= 2) {
        if(i == 0) {
            *target = CONTENTS;
            return "Contents";
        }
        i--;
    }
    *target = p->menu_target[i];
    return p->menu_label[i];
}

static uint8_t overlay_count(const Page* p) {
    int16_t at[MENU_CAP * 8];
    return p->overlay == OverlayMenu ? menu_count(p) : outline(p, at, MENU_CAP * 8);
}

static const char* overlay_label(const Page* p, uint8_t i, char* buf, size_t cap) {
    uint32_t target;
    if(p->overlay == OverlayMenu) return menu_entry(p, i, &target);
    int16_t at[MENU_CAP * 8];
    outline(p, at, MENU_CAP * 8);
    const Line* l = &p->lines[at[i]];
    const Block* b = &p->blocks[l->block];
    size_t n = b->len < cap - 1 ? b->len : cap - 1;
    memcpy(buf, p->text + b->off, n);
    buf[n] = '\0';
    return buf;
}

#define PANEL_X    12
#define PANEL_Y    3
#define PANEL_W    104
#define PANEL_H    58
#define PANEL_ROWS 4
#define PANEL_ROW  11

/* A panel over a stippled page: title strip, four rows, the selected one inverted. */
static void draw_overlay(Canvas* canvas, const Page* p) {
    char buf[64], fitted[64];
    for(int y = 0; y < SCREEN_H; y += 2) {
        for(int x = y % 4 == 0 ? 0 : 1; x < SCREEN_W; x += 2)
            canvas_draw_dot(canvas, x, y);
    }
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 3);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 3);
    canvas_draw_rbox(canvas, PANEL_X, PANEL_Y, PANEL_W, 12, 3);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(
        canvas, PANEL_X + 5, PANEL_Y + 10, p->overlay == OverlayMenu ? "Actions" : "Contents");
    canvas_set_color(canvas, ColorBlack);
    set_role(canvas, RoleBody);
    uint8_t n = overlay_count(p);
    int first = p->overlay_sel > PANEL_ROWS - 1 ? p->overlay_sel - (PANEL_ROWS - 1) : 0;
    for(int r = 0; r < PANEL_ROWS && first + r < n; r++) {
        int i = first + r, y = PANEL_Y + 13 + r * PANEL_ROW;
        const char* label = overlay_label(p, (uint8_t)i, buf, sizeof(buf));
        fit(canvas, label, strlen(label), PANEL_W - 12, fitted, sizeof(fitted));
        if(i == p->overlay_sel) {
            canvas_draw_rbox(canvas, PANEL_X + 2, y, PANEL_W - 4, PANEL_ROW, 2);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, PANEL_X + 6, y + 9, fitted);
        canvas_set_color(canvas, ColorBlack);
    }
    if(n > PANEL_ROWS) {
        int track = PANEL_ROWS * PANEL_ROW, thumb = track * PANEL_ROWS / n;
        int ty = PANEL_Y + 13 + (track - thumb) * first / (n - PANEL_ROWS);
        canvas_draw_box(canvas, PANEL_X + PANEL_W - 3, ty, 1, (size_t)thumb);
    }
}

/* Node box for side k (0 centre, 1 top, 2 right, 3 bottom, 4 left). */
static void node_box(int k, int* x, int* y, int* w, int* h) {
    static const int8_t box[5][4] = {
        {36, 31, 56, 13}, {14, 14, 100, 11}, {95, 32, 33, 11}, {14, 52, 100, 11}, {0, 32, 33, 11}};
    *x = box[k][0];
    *y = box[k][1];
    *w = box[k][2];
    *h = box[k][3];
}

static int node_at(const Page* p, int side) {
    int i = p->node_window + side;
    return side == 0 ? 0 : i <= p->nodes_n ? i : -1;
}

static void draw_edge(Canvas* canvas, int x1, int y1, int x2, int y2, bool dotted) {
    if(!dotted) {
        canvas_draw_line(canvas, x1, y1, x2, y2);
        return;
    }
    int steps = (x2 > x1 ? x2 - x1 : x1 - x2) + (y2 > y1 ? y2 - y1 : y1 - y2);
    for(int t = 0; t <= steps; t += 2)
        canvas_draw_dot(canvas, x1 + (x2 - x1) * t / steps, y1 + (y2 - y1) * t / steps);
}

static void draw_graph(Canvas* canvas, const Page* p) {
    static const int8_t anchor[5][4] = {
        /* edge from the centre box to each side: centre point, then node point */
        {0, 0, 0, 0},
        {64, 31, 64, 25},
        {92, 37, 95, 37},
        {64, 44, 64, 52},
        {36, 37, 33, 37}};
    char fitted[LABEL_CAP + 4];
    canvas_set_font(canvas, FontSecondary);
    for(int side = 1; side <= 4; side++) {
        int i = node_at(p, side);
        if(i < 0) continue;
        draw_edge(
            canvas,
            anchor[side][0],
            anchor[side][1],
            anchor[side][2],
            anchor[side][3],
            p->node_kind[i] == 1);
    }
    for(int side = 0; side <= 4; side++) {
        int i = node_at(p, side), x, y, w, h;
        if(i < 0) continue;
        node_box(side, &x, &y, &w, &h);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y, (size_t)w, (size_t)h);
        canvas_set_color(canvas, ColorBlack);
        bool focused = p->node_focus == side;
        if(focused || side == 0) {
            canvas_draw_rbox(canvas, x, y, (size_t)w, (size_t)h, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, y, (size_t)w, (size_t)h, 2);
        }
        if(side == 0 && !focused) {
            /* the centre is solid; unfocused it gets an inner frame instead of inversion */
            canvas_draw_rframe(canvas, x + 1, y + 1, (size_t)(w - 2), (size_t)(h - 2), 2);
        }
        canvas_set_font(canvas, side == 0 ? FontPrimary : FontSecondary);
        const char* label = p->node_label[i];
        fit(canvas, label, strlen(label), w - 6, fitted, sizeof(fitted));
        int tw = canvas_string_width(canvas, fitted);
        canvas_draw_str(canvas, x + (w - tw) / 2, y + h - 3, fitted);
        canvas_set_color(canvas, ColorBlack);
    }
}

void page_draw(Page* p, Canvas* canvas) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(p->dirty) layout(p, canvas);
    if(p->graph) {
        draw_graph(canvas, p);
        draw_header(canvas, p);
        return;
    }
    int y = HEADER_H, bottom = body_bottom(p);
    for(int16_t i = p->top; i < (int16_t)p->lines_n && y < bottom; i++) {
        draw_line(canvas, p, &p->lines[i], y);
        y += p->lines[i].height;
    }
    /* clear what spilled over the edges, then draw the chrome on top */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, SCREEN_W, HEADER_H);
    if(has_softkeys(p)) canvas_draw_box(canvas, 0, bottom, SCREEN_W, SCREEN_H - bottom);
    canvas_set_color(canvas, ColorBlack);
    draw_header(canvas, p);
    draw_scrollbar(canvas, p);
    for(int k = 0; k < 3; k++)
        draw_softkey(canvas, k, p->softkey[k]);
    if(p->overlay != OverlayNone) draw_overlay(canvas, p);
}

/* ------------------------------------------------------------------ input */

static int16_t next_selectable(const Page* p, int16_t from, int dir) {
    for(int16_t b = (int16_t)(from + dir); b >= 0 && b < (int16_t)p->blocks_n; b += dir) {
        if(selectable(&p->blocks[b])) return b;
    }
    return -1;
}

static bool block_visible(const Page* p, int16_t b) {
    int16_t first = first_line_of(p, b);
    return first >= p->top && first <= last_visible(p, p->top);
}

/* The run of consecutive chips around block b. */
static void chip_run(const Page* p, int16_t b, int16_t* first, int16_t* last) {
    *first = *last = b;
    while(*first > 0 && p->blocks[*first - 1].style == PageChip)
        (*first)--;
    while(*last + 1 < (int16_t)p->blocks_n && p->blocks[*last + 1].style == PageChip)
        (*last)++;
}

static void step(Page* p, int dir) {
    int16_t mt = max_top(p);
    if(p->list) {
        int16_t n = next_selectable(p, p->focus, dir);
        if(n < 0) n = next_selectable(p, dir > 0 ? -1 : (int16_t)p->blocks_n, dir); /* wrap */
        if(n >= 0) {
            p->focus = n;
            if(dir < 0 && first_line_of(p, n) > last_visible(p, p->top)) p->top = mt;
            reveal(p, n);
        }
        return;
    }
    /* documents: the next link in view takes the focus, else the text moves one line */
    int16_t from = p->focus;
    if(from >= 0 && p->blocks[from].style == PageChip) {
        /* Up/Down leave a row of chips as a whole */
        int16_t first, last;
        chip_run(p, from, &first, &last);
        from = dir > 0 ? last : first;
    }
    if(from < 0) {
        from = p->lines_n ? (int16_t)p->lines[dir > 0 ? p->top : last_visible(p, p->top)].block :
                            0;
        from = (int16_t)(from - dir);
    }
    int16_t n = next_selectable(p, from, dir);
    if(n >= 0 && block_visible(p, n)) {
        p->focus = n;
        reveal(p, n);
        return;
    }
    /* a press always moves text: gaps (lines without content flags) are skipped */
    if(dir > 0 && p->top < mt) {
        p->top++;
        while(p->top < mt && is_gap(&p->lines[p->top]))
            p->top++;
    }
    if(dir < 0 && p->top > 0) {
        p->top--;
        while(p->top > 0 && is_gap(&p->lines[p->top]))
            p->top--;
    }
    if(p->focus >= 0 && !block_visible(p, p->focus)) p->focus = -1;
    /* a link that has just come into view takes the focus, so OK opens it at once */
    if(n >= 0 && block_visible(p, n)) p->focus = n;
}

static bool is_heading_line(const Page* p, int16_t i) {
    const Line* l = &p->lines[i];
    return p->blocks[l->block].style == PageHeading && (l->flags & LINE_FIRST);
}

/* Left/Right: the next or previous section. Its heading moves into the header (the header shows
 * the section whose heading scrolled by), so the body starts with the section's own text. */
static void jump(Page* p, int dir) {
    int16_t mt = max_top(p);
    if(p->focus >= 0 && p->blocks[p->focus].style == PageChip) {
        /* Left/Right move along a row of chips */
        int16_t n = (int16_t)(p->focus + dir);
        if(n >= 0 && n < (int16_t)p->blocks_n && p->blocks[n].style == PageChip) {
            p->focus = n;
            return;
        }
        /* past the last chip, Left/Right jump sections as anywhere else */
    }
    if(p->list) {
        for(int k = 0; k < 4; k++) {
            int16_t n = next_selectable(p, p->focus, dir);
            if(n < 0) break;
            p->focus = n;
        }
        if(p->focus >= 0) reveal(p, p->focus);
        return;
    }
    /* the heading of the section in view: the last one above the first visible line */
    int16_t at = -1;
    for(int16_t i = 0; i < p->top && i < (int16_t)p->lines_n; i++) {
        if(is_heading_line(p, i)) at = i;
    }
    int16_t target = -1;
    if(dir > 0) {
        for(int16_t i = (int16_t)(at + 1); i < (int16_t)p->lines_n; i++) {
            if(is_heading_line(p, i)) {
                target = (int16_t)(i + 1);
                break;
            }
        }
        if(target < 0) target = mt;
    } else {
        target = 0;
        for(int16_t i = (int16_t)(at - 1); i >= 0; i--) {
            if(is_heading_line(p, i)) {
                target = (int16_t)(i + 1);
                break;
            }
        }
    }
    p->top = target > mt ? mt : target;
    p->focus = -1;
}

static PageAction overlay_input(Page* p, PageKey key, PagePress press) {
    PageAction act = {PageActNone, 0, false};
    int n = overlay_count(p);
    if(key == PageKeyBack || (key == PageKeyOk && press == PagePressLong)) {
        p->overlay = OverlayNone; /* dismiss; Back does not leave the page */
    } else if(key == PageKeyUp || key == PageKeyDown) {
        p->overlay_sel = (int16_t)((p->overlay_sel + (key == PageKeyDown ? 1 : n - 1)) % n);
    } else if(key == PageKeyOk && p->overlay == OverlayMenu) {
        uint32_t target;
        menu_entry(p, (uint8_t)p->overlay_sel, &target);
        if(target == CONTENTS) {
            /* open the outline on the section in view */
            uint16_t index, count;
            current_heading(p, &index, &count);
            p->overlay = OverlayOutline;
            p->overlay_sel = (int16_t)(index ? index - 1 : 0);
        } else {
            p->overlay = OverlayNone;
            act.kind = PageActOpen;
            act.target = target;
        }
    } else if(key == PageKeyOk) {
        /* outline: the chosen section's heading moves into the header */
        int16_t at[MENU_CAP * 8];
        outline(p, at, MENU_CAP * 8);
        int16_t mt = max_top(p), top = (int16_t)(at[p->overlay_sel] + 1);
        p->top = top > mt ? mt : top;
        p->focus = -1;
        p->overlay = OverlayNone;
    }
    return act;
}

static PageAction graph_input(Page* p, PageKey key, PagePress press) {
    static const int8_t side_of[] = {
        [PageKeyUp] = 1, [PageKeyRight] = 2, [PageKeyDown] = 3, [PageKeyLeft] = 4};
    PageAction act = {PageActNone, 0, false};
    if(key == PageKeyOk) {
        int i = node_at(p, p->node_focus);
        act.kind = PageActOpen;
        act.target = p->node_target[i];
        act.long_press = press == PagePressLong && p->node_focus != 0;
        return act;
    }
    int side = side_of[key], focus = p->node_focus;
    int opposite = focus > 2 ? focus - 2 : focus + 2;
    if(focus != 0 && side == focus) {
        /* the same direction again: the next four neighbours (wrapping) */
        p->node_window = (uint8_t)(p->node_window + 4 < p->nodes_n ? p->node_window + 4 : 0);
        if(node_at(p, focus) < 0) p->node_focus = 0;
    } else if(focus != 0 && side == opposite) {
        p->node_focus = 0;
    } else if(node_at(p, side) >= 0) {
        p->node_focus = (int8_t)side;
    }
    return act;
}

PageAction page_input(Page* p, PageKey key, PagePress press) {
    PageAction act = {PageActNone, 0, false};
    if(p->overlay != OverlayNone) return overlay_input(p, key, press);
    if(key == PageKeyBack) {
        if(press == PagePressShort) act.kind = PageActBack;
        return act;
    }
    if(p->dirty) return act; /* not laid out yet: nothing on screen to act on */
    if(p->graph) return graph_input(p, key, press);
    if(key == PageKeyOk && press == PagePressLong && !has_softkeys(p) && menu_count(p) > 0) {
        p->overlay = OverlayMenu;
        p->overlay_sel = 0;
        return act;
    }
    bool soft = has_softkeys(p);
    if(soft && (key == PageKeyLeft || key == PageKeyRight || key == PageKeyOk)) {
        int slot = key == PageKeyLeft ? 0 : key == PageKeyOk ? 1 : 2;
        bool ok = press == PagePressShort || (press == PagePressLong && slot == 1);
        if(p->softkey[slot][0] && ok) {
            act.kind = PageActSoftkey;
            act.target = (uint32_t)slot;
            act.long_press = press == PagePressLong;
        }
        return act;
    }
    if(key == PageKeyOk) {
        if(press == PagePressShort && p->focus >= 0) {
            act.kind = PageActOpen;
            act.target = p->blocks[p->focus].target;
        }
        return act;
    }
    int dir = (key == PageKeyDown || key == PageKeyRight) ? 1 : -1;
    if(key == PageKeyUp || key == PageKeyDown) {
        if(press == PagePressLong)
            jump(p, dir);
        else
            step(p, dir);
    } else if(press != PagePressLong) {
        jump(p, dir);
    }
    return act;
}

uint16_t page_visible_lines(const Page* p, char out[][64], uint16_t cap) {
    uint16_t n = 0;
    int16_t last = p->dirty ? -1 : last_visible(p, p->top);
    if(cap < 3 || p->dirty) return 0;
    const char* t;
    size_t tn;
    char pos[POS_CAP];
    header_of(p, &t, &tn, pos);
    if(tn > 40) tn = 40;
    memcpy(out[n], "# ", 2);
    memcpy(out[n] + 2, t, tn);
    out[n][2 + tn] = '\0';
    if(pos[0]) {
        size_t k = strlen(out[n]);
        out[n][k++] = ' ';
        out[n][k++] = '[';
        size_t pn = strlen(pos);
        memcpy(out[n] + k, pos, pn);
        out[n][k + pn] = ']';
        out[n][k + pn + 1] = '\0';
    }
    n++;
    cap--; /* one entry kept for the softkeys */
    if(p->graph) {
        /* the map: "o" the centre, then the visible neighbours by side */
        static const char mark[5] = {'o', '^', '>', 'v', '<'};
        for(int side = 0; side <= 4 && n < cap; side++) {
            int i = node_at(p, side);
            if(i < 0) continue;
            out[n][0] = p->node_focus == side ? '*' : ' ';
            out[n][1] = mark[side];
            out[n][2] = ' ';
            copy_str(out[n] + 3, p->node_label[i], 60);
            n++;
        }
        return n;
    }
    if(p->overlay != OverlayNone) {
        char buf[64];
        copy_str(out[n++], p->overlay == OverlayMenu ? "[Actions]" : "[Contents]", 64);
        for(uint8_t i = 0; i < overlay_count(p) && n < cap; i++) {
            out[n][0] = i == p->overlay_sel ? '>' : ' ';
            copy_str(out[n] + 1, overlay_label(p, i, buf, sizeof(buf)), 62);
            n++;
        }
        return n;
    }
    for(int16_t i = p->top; i <= last && n < cap; i++) {
        const Line* l = &p->lines[i];
        const Block* blk = &p->blocks[l->block];
        bool whole = blk->style == PageRow || blk->style == PageBig || blk->style == PageBar ||
                     blk->style == PageSteps || blk->style == PageBadge || blk->style == PageChip;
        const char* s = p->text + (whole ? blk->off : l->off);
        size_t len = whole ? blk->len : l->len;
        if(len == 0) continue;
        if(len > 62) len = 62;
        out[n][0] = p->focus == (int16_t)l->block ? '>' : ' ';
        size_t k = 1;
        for(size_t c = 0; c < len && k < 62; c++) {
            if(s[c] == '\t') { /* a badge: "kind - caption" */
                memcpy(out[n] + k, " - ", 3);
                k += 3;
            } else {
                out[n][k++] = s[c];
            }
        }
        out[n][k] = '\0';
        n++;
    }
    if(has_softkeys(p)) {
        size_t k = 0;
        out[n][k++] = '[';
        for(int i = 0; i < 3; i++) {
            size_t ln = strlen(p->softkey[i]);
            memcpy(out[n] + k, p->softkey[i], ln);
            k += ln;
            out[n][k++] = i < 2 ? '|' : ']';
        }
        out[n][k] = '\0';
        n++;
    }
    return n;
}
