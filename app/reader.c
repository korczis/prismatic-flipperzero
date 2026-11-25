#include "reader.h"

#include <furi.h>
#include <gui/canvas.h>
#include <gui/elements.h>

#define TEXT_CAP 12288u
#define BLOCK_CAP 200u
#define LINE_CAP 800u
#define LEFT 2 /* page margin */
#define RIGHT 124 /* last text column; the scrollbar lives right of it */
#define SCREEN_H 64
#define BAR_H 12 /* button bar height when buttons are shown */
#define LABEL_CAP 12

typedef struct {
    uint8_t style;
    uint16_t off;
    uint16_t len;
    uint32_t target;
} Block;

typedef struct {
    uint16_t block;
    uint16_t off;
    uint8_t len;
    uint8_t height;
    uint8_t first; /* first line of its block */
} Line;

typedef struct {
    char* text;
    uint16_t text_len;
    Block* blocks;
    uint16_t blocks_n;
    Line* lines;
    uint16_t lines_n;
    bool dirty;
    bool truncated;
    int16_t top;
    int16_t focus; /* index of a ReaderLink block, or -1 */
    char button[3][LABEL_CAP]; /* left, center, right; empty = none */
} Model;

struct Reader {
    View* view;
    ReaderLinkCallback callback;
    void* context;
    ReaderButtonCallback button_callback;
    void* button_context;
};

/* Typography: one table, so every style is decided in one place. */
static Font font_of(uint8_t style) {
    return style == ReaderTitle ? FontPrimary : FontSecondary;
}

static uint8_t indent_of(uint8_t style) {
    switch(style) {
    case ReaderBullet:
        return LEFT + 7;
    case ReaderLink:
        return LEFT + 8;
    case ReaderNote:
        return LEFT + 5;
    case ReaderHeading:
        return LEFT + 3;
    default:
        return LEFT;
    }
}

static uint8_t height_of(uint8_t style) {
    return style == ReaderTitle ? 13 : style == ReaderHeading ? 12 : 11;
}

static bool has_buttons(const Model* m) {
    return m->button[0][0] || m->button[1][0] || m->button[2][0];
}

static int view_height(const Model* m) {
    return has_buttons(m) ? SCREEN_H - BAR_H : SCREEN_H;
}

static void push_line(Model* m, uint16_t block, uint16_t off, uint16_t len, uint8_t h, bool first) {
    if(m->lines_n >= LINE_CAP) return;
    Line* l = &m->lines[m->lines_n++];
    l->block = block;
    l->off = off;
    l->len = (uint8_t)(len > 255 ? 255 : len);
    l->height = h;
    l->first = first;
}

static void push_gap(Model* m, uint16_t block, uint8_t h) {
    push_line(m, block, 0, 0, h, false);
}

/* Greedy word wrap with the fonts' real glyph widths; a word longer than a line is broken. */
static void layout(Canvas* canvas, Model* m) {
    m->lines_n = 0;
    for(uint16_t b = 0; b < m->blocks_n; b++) {
        const Block* blk = &m->blocks[b];
        canvas_set_font(canvas, font_of(blk->style));
        int avail = RIGHT - indent_of(blk->style) - (blk->style == ReaderHeading ? 3 : 0);
        const char* s = m->text + blk->off;
        uint16_t start = 0, last_space = 0, i = 0;
        int width = 0;
        bool first = true, have_space = false;
        if(blk->style == ReaderHeading && b > 0) push_gap(m, b, 3);
        while(i < blk->len) {
            int w = (int)canvas_glyph_width(canvas, (uint8_t)s[i]);
            if(width + w > avail && i > start) {
                uint16_t cut = have_space ? last_space : i;
                push_line(m, b, blk->off + start, cut - start, height_of(blk->style), first);
                first = false;
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
        if(i > start || first) {
            push_line(m, b, blk->off + start, i - start, height_of(blk->style), first);
        }
        if(blk->style == ReaderTitle) {
            push_gap(m, b, 4); /* room for the rule under the title */
            continue;
        }
        bool run = b + 1 < m->blocks_n && m->blocks[b + 1].style == blk->style &&
                   (blk->style == ReaderBullet || blk->style == ReaderLink);
        if(!run && blk->style != ReaderHeading) push_gap(m, b, 3);
    }
    m->dirty = false;
}

static int16_t max_top(const Model* m) {
    int h = 0, vh = view_height(m);
    for(int16_t i = (int16_t)m->lines_n - 1; i >= 0; i--) {
        h += m->lines[i].height;
        if(h > vh) return (int16_t)(i + 1);
    }
    return 0;
}

static void draw_line(Canvas* canvas, const Model* m, const Line* l, int y, char* buf, bool last) {
    const Block* blk = &m->blocks[l->block];
    if(l->len == 0) {
        /* the rule under a title sits in the title's trailing gap */
        if(blk->style == ReaderTitle && l->height == 4) {
            canvas_draw_line(canvas, LEFT, y + 1, RIGHT, y + 1);
        }
        return;
    }
    memcpy(buf, m->text + l->off, l->len);
    buf[l->len] = '\0';
    canvas_set_font(canvas, font_of(blk->style));
    int x = indent_of(blk->style);
    int base = y + l->height - 3; /* baseline */
    switch(blk->style) {
    case ReaderTitle:
        canvas_draw_str(canvas, x, base + 1, buf);
        break;
    case ReaderHeading: {
        /* a compact inverted label, as wide as its text */
        int w = canvas_string_width(canvas, buf);
        canvas_draw_rbox(canvas, LEFT, y + 1, (size_t)(w + 6), (size_t)(l->height - 2), 2);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, x, base, buf);
        canvas_set_color(canvas, ColorBlack);
        break;
    }
    case ReaderMeta:
        canvas_draw_str(canvas, x, base, buf);
        if(last) {
            for(int dx = LEFT; dx < RIGHT; dx += 3) canvas_draw_dot(canvas, dx, y + l->height - 1);
        }
        break;
    case ReaderBullet:
        if(l->first) canvas_draw_box(canvas, LEFT + 2, base - 4, 3, 3);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case ReaderNote:
        canvas_draw_line(canvas, LEFT + 1, y, LEFT + 1, y + l->height - 1);
        canvas_draw_str(canvas, x, base, buf);
        break;
    case ReaderLink: {
        bool focused = m->focus == (int16_t)l->block;
        if(focused) {
            canvas_draw_rbox(canvas, LEFT, y, RIGHT - LEFT + 1, l->height, 2);
            canvas_set_color(canvas, ColorWhite);
        }
        if(l->first) {
            /* a small chevron drawn from lines, independent of the font */
            int ay = base - 3;
            canvas_draw_line(canvas, LEFT + 2, ay - 2, LEFT + 4, ay);
            canvas_draw_line(canvas, LEFT + 4, ay, LEFT + 2, ay + 2);
            canvas_draw_line(canvas, LEFT + 3, ay - 2, LEFT + 5, ay);
            canvas_draw_line(canvas, LEFT + 5, ay, LEFT + 3, ay + 2);
        }
        canvas_draw_str(canvas, x, base, buf);
        canvas_set_color(canvas, ColorBlack);
        break;
    }
    default:
        canvas_draw_str(canvas, x, base, buf);
        break;
    }
}

static void draw_cb(Canvas* canvas, void* model) {
    Model* m = model;
    canvas_clear(canvas);
    if(m->dirty) layout(canvas, m);
    int y = 0, vh = view_height(m);
    char buf[256];
    for(uint16_t i = (uint16_t)m->top; i < m->lines_n && y < vh; i++) {
        bool last = i + 1 >= m->lines_n || m->lines[i + 1].block != m->lines[i].block ||
                    m->lines[i + 1].len == 0;
        draw_line(canvas, m, &m->lines[i], y, buf, last);
        y += m->lines[i].height;
    }
    int16_t mt = max_top(m);
    if(mt > 0) {
        elements_scrollbar_pos(canvas, 128, 0, (size_t)vh, (uint16_t)m->top, (uint16_t)(mt + 1));
    }
    if(has_buttons(m)) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, vh, 128, BAR_H); /* keep text out of the bar */
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);
        if(m->button[0][0]) elements_button_left(canvas, m->button[0]);
        if(m->button[1][0]) elements_button_center(canvas, m->button[1]);
        if(m->button[2][0]) elements_button_right(canvas, m->button[2]);
    }
}

static int16_t line_of(const Model* m, int16_t b) {
    for(uint16_t i = 0; i < m->lines_n; i++) {
        if(m->lines[i].block == (uint16_t)b && m->lines[i].first) return (int16_t)i;
    }
    return 0;
}

static int16_t next_link(const Model* m, int16_t from, int dir) {
    for(int16_t b = (int16_t)(from + dir); b >= 0 && b < (int16_t)m->blocks_n; b += dir) {
        if(m->blocks[b].style == ReaderLink) return b;
    }
    return -1;
}

static bool input_cb(InputEvent* event, void* context) {
    Reader* r = context;
    bool consumed = true;
    uint32_t open = UINT32_MAX;
    int button = -1;
    with_view_model(
        r->view,
        Model * m,
        {
            int16_t mt = max_top(m);
            bool scroll = event->type == InputTypeShort || event->type == InputTypeRepeat;
            if(has_buttons(m) && (event->key == InputKeyLeft || event->key == InputKeyRight ||
                                  event->key == InputKeyOk)) {
                int idx = event->key == InputKeyLeft ? 0 : event->key == InputKeyOk ? 1 : 2;
                if(m->button[idx][0] &&
                   (event->type == InputTypeShort ||
                    (event->type == InputTypeLong && event->key == InputKeyOk))) {
                    button = idx;
                }
            } else if(!scroll) {
                consumed = false;
            } else if(event->key == InputKeyUp) {
                if(m->top > 0) m->top--;
            } else if(event->key == InputKeyDown) {
                if(m->top < mt) m->top++;
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                int dir = event->key == InputKeyRight ? 1 : -1;
                int16_t from = m->focus;
                if(from < 0 && m->lines_n) {
                    int16_t tb = (int16_t)m->lines[m->top].block;
                    from = dir > 0 ? (int16_t)(tb - 1) : (int16_t)(tb + 1);
                }
                int16_t n = next_link(m, from, dir);
                if(n >= 0) {
                    m->focus = n;
                    int16_t ln = line_of(m, n);
                    if(ln < m->top) m->top = ln;
                    else if(ln > m->top + 3) m->top = ln - 3 > mt ? mt : (int16_t)(ln - 3);
                } else {
                    m->top = dir > 0 ? mt : 0;
                }
            } else if(event->key == InputKeyOk) {
                if(m->focus >= 0) open = m->blocks[m->focus].target;
            } else {
                consumed = false;
            }
        },
        true);
    if(button >= 0 && r->button_callback) {
        r->button_callback(r->button_context, (uint8_t)button, event->type == InputTypeLong);
    }
    if(open != UINT32_MAX && r->callback) r->callback(r->context, open);
    return consumed;
}

Reader* reader_alloc(void) {
    Reader* r = malloc(sizeof(Reader));
    memset(r, 0, sizeof(Reader));
    r->view = view_alloc();
    view_allocate_model(r->view, ViewModelTypeLocking, sizeof(Model));
    view_set_context(r->view, r);
    view_set_draw_callback(r->view, draw_cb);
    view_set_input_callback(r->view, input_cb);
    with_view_model(
        r->view,
        Model * m,
        {
            memset(m, 0, sizeof(Model));
            m->text = malloc(TEXT_CAP);
            m->blocks = malloc(sizeof(Block) * BLOCK_CAP);
            m->lines = malloc(sizeof(Line) * LINE_CAP);
            m->dirty = true;
            m->focus = -1;
        },
        false);
    return r;
}

void reader_free(Reader* r) {
    with_view_model(
        r->view,
        Model * m,
        {
            free(m->text);
            free(m->blocks);
            free(m->lines);
        },
        false);
    view_free(r->view);
    free(r);
}

View* reader_get_view(Reader* r) {
    return r->view;
}

void reader_set_link_callback(Reader* r, ReaderLinkCallback callback, void* context) {
    r->callback = callback;
    r->context = context;
}

void reader_set_button_callback(Reader* r, ReaderButtonCallback callback, void* context) {
    r->button_callback = callback;
    r->button_context = context;
}

void reader_reset(Reader* r) {
    with_view_model(
        r->view,
        Model * m,
        {
            m->text_len = 0;
            m->blocks_n = 0;
            m->lines_n = 0;
            m->top = 0;
            m->focus = -1;
            m->dirty = true;
            m->truncated = false;
            memset(m->button, 0, sizeof(m->button));
        },
        false);
}

void reader_set_buttons(Reader* r, const char* left, const char* center, const char* right) {
    with_view_model(
        r->view,
        Model * m,
        {
            strlcpy(m->button[0], left ? left : "", LABEL_CAP);
            strlcpy(m->button[1], center ? center : "", LABEL_CAP);
            strlcpy(m->button[2], right ? right : "", LABEL_CAP);
            m->dirty = true;
        },
        false);
}

static void add_locked(Model* m, ReaderStyle style, const char* text, uint32_t target) {
    size_t n = strlen(text);
    while(n && text[n - 1] == ' ') n--;
    if(m->truncated || n == 0) return;
    if((uint32_t)m->blocks_n + 1 >= BLOCK_CAP || m->text_len + n + 32 >= TEXT_CAP) {
        m->truncated = true;
        const char* note = "(more in the full pack text)";
        size_t k = strlen(note);
        memcpy(m->text + m->text_len, note, k);
        m->blocks[m->blocks_n++] = (Block){ReaderNote, m->text_len, (uint16_t)k, 0};
        m->text_len += (uint16_t)k;
        return;
    }
    memcpy(m->text + m->text_len, text, n);
    m->blocks[m->blocks_n++] = (Block){(uint8_t)style, m->text_len, (uint16_t)n, target};
    m->text_len += (uint16_t)n;
}

void reader_add(Reader* r, ReaderStyle style, const char* text, uint32_t link_target) {
    with_view_model(r->view, Model * m, { add_locked(m, style, text, link_target); }, false);
}

void reader_show(Reader* r) {
    with_view_model(
        r->view,
        Model * m,
        {
            m->dirty = true;
            m->top = 0;
            m->focus = -1;
        },
        true);
}
