#include "page.h"

#include "icons.h"

#include <stdlib.h>
#include <string.h>

#define TEXT_CAP  12288u
#define BLOCK_CAP 220u
#define LINE_CAP  900u
#define TITLE_CAP 48u
#define POS_CAP   12u
#define KEY_CAP   10u

/* Geometry: docs/ui.md explains every number. */
#define SCREEN_W  128
#define SCREEN_H  64
#define HEADER_H  13 /* title baseline 9, rule at y 11, body from y 13 */
#define SOFT_H    12
#define LEFT      2
#define RIGHT     123 /* last text column; the scrollbar is 125..127 */
#define LEADING   10
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
            (Block){PageNote, 0, p->text_len, (uint16_t)(sizeof(note) - 1), 0, 0, 0, 0, 0};
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
        (Block){(uint8_t)style, 0, (uint16_t)at, (uint16_t)n, 0, 0, target, 0, 0};
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
        PageRow, icon, (uint16_t)at, (uint16_t)n, (uint16_t)rat, (uint8_t)rn, target, 0, 0};
    p->list = true;
    p->dirty = true;
}

static bool selectable(const Block* b) {
    return b->style == PageLink || b->style == PageRow;
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

static Font font_of(uint8_t style) {
    return style == PageHeading || style == PageTitle ? FontPrimary : FontSecondary;
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
    uint16_t start = 0, last_space = 0, i = 0, first_line = p->lines_n;
    int width = 0;
    bool have_space = false;
    canvas_set_font(canvas, font_of(blk->style));
    while(i < blk->len) {
        int w = (int)canvas_glyph_width(canvas, (uint8_t)s[i]);
        if(width + w > avail && i > start) {
            uint16_t cut = have_space ? last_space : i;
            push_line(p, b, blk->off + start, cut - start, height, LINE_BODY);
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
            wrap(p, canvas, b, LEADING);
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

static void draw_bar(Canvas* canvas, const char* spec, size_t n, int y) {
    int v[4] = {0, 0, 0, 0}, k = 0, total = 0;
    for(size_t i = 0; i < n && k < 4; i++) {
        if(spec[i] == ',')
            k++;
        else if(spec[i] >= '0' && spec[i] <= '9')
            v[k] = v[k] * 10 + (spec[i] - '0');
    }
    for(int i = 0; i < 4; i++)
        total += v[i];
    int x = LEFT, w = RIGHT - LEFT + 1, h = 8;
    canvas_draw_rframe(canvas, x, y + 1, (size_t)w, (size_t)h, 1);
    if(total == 0) return;
    int at = x + 1, inner = w - 2;
    for(int i = 0; i < 4; i++) {
        int seg = (v[i] * inner + total / 2) / total;
        if(i == 3) seg = x + 1 + inner - at;
        if(seg <= 0) continue;
        draw_fill(canvas, i, at, y + 2, seg, h - 2);
        at += seg;
        if(at < x + w - 1) canvas_draw_line(canvas, at, y + 2, at, y + h - 1);
    }
}

/* The legend under a bar: a swatch, the count and its name for every share. */
static void draw_legend(Canvas* canvas, const char* spec, size_t n, int y) {
    static const char* const names[4] = {"seen", "no", "?", "n/a"};
    int v[4] = {0, 0, 0, 0}, k = 0, x = LEFT;
    for(size_t i = 0; i < n && k < 4; i++) {
        if(spec[i] == ',')
            k++;
        else if(spec[i] >= '0' && spec[i] <= '9')
            v[k] = v[k] * 10 + (spec[i] - '0');
    }
    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < 4; i++) {
        char label[16];
        int len = 0, val = v[i];
        char digits[6];
        int d = 0;
        do {
            digits[d++] = (char)('0' + val % 10);
            val /= 10;
        } while(val && d < 5);
        while(d)
            label[len++] = digits[--d];
        label[len++] = ' ';
        for(const char* c = names[i]; *c; c++)
            label[len++] = *c;
        label[len] = '\0';
        canvas_draw_frame(canvas, x, y + 2, 6, 6);
        draw_fill(canvas, i, x + 1, y + 3, 4, 4);
        canvas_draw_str(canvas, x + 8, y + 8, label);
        x += 8 + canvas_string_width(canvas, label) + 6;
    }
}

static void draw_row(Canvas* canvas, const Page* p, const Block* blk, int y, bool focused) {
    char buf[256];
    int x = LEFT + 1;
    if(focused) {
        canvas_draw_rbox(canvas, 0, y, RIGHT + 1, ROW_H, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    if(blk->icon) {
        draw_icon(canvas, blk->icon, x + 1, y + 3);
        x += 12;
    }
    int rw = 0;
    if(blk->right_len) {
        memcpy(buf, p->text + blk->right_off, blk->right_len);
        buf[blk->right_len] = '\0';
        rw = canvas_string_width(canvas, buf);
        canvas_draw_str(canvas, RIGHT - 2 - rw, y + 9, buf);
        rw += 4;
    }
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
        draw_bar(canvas, p->text + blk->off, blk->len, y);
        draw_legend(canvas, p->text + blk->off, blk->len, y + 10);
        return;
    case PageSteps:
        draw_steps(canvas, p->text + blk->off, blk->len, y);
        return;
    default:
        break;
    }
    memcpy(buf, p->text + l->off, l->len);
    buf[l->len] = '\0';
    canvas_set_font(canvas, font_of(blk->style));
    int x = indent_of(blk->style), base = y + 8;
    switch(blk->style) {
    case PageHeading:
        canvas_draw_box(canvas, 0, y + 1, RIGHT + 1, HEADING_H - 2);
        canvas_set_color(canvas, ColorWhite);
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
                canvas_draw_dot(canvas, dx, y + LEADING);
        }
        break;
    case PageBullet:
        if(l->flags & LINE_FIRST) canvas_draw_box(canvas, LEFT + 1, base - 4, 2, 2);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageNote:
        canvas_draw_line(canvas, LEFT, y, LEFT, y + LEADING - 1);
        canvas_draw_line(canvas, LEFT + 1, y, LEFT + 1, y + LEADING - 1);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageQuote:
        canvas_draw_line(canvas, LEFT, y, LEFT, y + LEADING - 1);
        canvas_draw_line(canvas, RIGHT, y, RIGHT, y + LEADING - 1);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case PageLink:
        if(focused) {
            canvas_draw_rbox(canvas, 0, y - 1, RIGHT + 1, LEADING + 1, 2);
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

/* The header's title (the current section once its heading scrolled by) and position. */
static int16_t header_of(const Page* p, const char** t, size_t* tn, char* pos) {
    uint16_t index, count;
    int16_t heading = current_heading(p, &index, &count);
    *t = p->title;
    *tn = strlen(p->title);
    if(heading >= 0) {
        *t = p->text + p->blocks[heading].off;
        *tn = p->blocks[heading].len;
    }
    pos[0] = '\0';
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
    if(p->title_icon && heading < 0) {
        draw_icon(canvas, p->title_icon, LEFT, 2);
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

void page_draw(Page* p, Canvas* canvas) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(p->dirty) layout(p, canvas);
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

PageAction page_input(Page* p, PageKey key, PagePress press) {
    PageAction act = {PageActNone, 0, false};
    if(key == PageKeyBack) {
        if(press == PagePressShort) act.kind = PageActBack;
        return act;
    }
    if(p->dirty) return act; /* not laid out yet: nothing on screen to act on */
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
    for(int16_t i = p->top; i <= last && n < cap; i++) {
        const Line* l = &p->lines[i];
        const Block* blk = &p->blocks[l->block];
        bool whole = blk->style == PageRow || blk->style == PageBig || blk->style == PageBar ||
                     blk->style == PageSteps;
        const char* s = p->text + (whole ? blk->off : l->off);
        size_t len = whole ? blk->len : l->len;
        if(len == 0) continue;
        if(len > 62) len = 62;
        out[n][0] = p->focus == (int16_t)l->block ? '>' : ' ';
        memcpy(out[n] + 1, s, len);
        out[n][len + 1] = '\0';
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
