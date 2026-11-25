#include "ui_app.h"

#include "../core/pocket.h"
#include "../version.h"
#include "icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_ROWS   40u
#define MAX_MARKERS 64u
#define STACK       24u
#define SCRATCH     (PKP_MAX_STRING + 1u)

/* Targets of selectable blocks: kind in the top byte, argument below. Zero means none. */
#define T_OBJECT    (1u << 24)
#define T_CHAPTER   (2u << 24) /* item offset inside the current object */
#define T_DETECT    (3u << 24)
#define T_QUIZ      (4u << 24)
#define T_NEXT_QUIZ (5u << 24) /* argument: quiz kind */
#define T_CATEGORY  (6u << 24)
#define T_HOME      (7u << 24) /* argument: a home entry */
#define T_PAGE      (8u << 24) /* argument: 0 previous, 1 next */
#define T_OPTION    (9u << 24) /* argument: quiz option */
#define T_MAP       (10u << 24) /* argument: object at the centre of a map */
#define T_KIND(t)   ((t) & 0xFF000000u)
#define T_ARG(t)    ((t) & 0x00FFFFFFu)

typedef enum {
    ScrHome,
    ScrCategories,
    ScrObjects,
    ScrFilter,
    ScrArticle,
    ScrChapter,
    ScrDetect,
    ScrDetectResult,
    ScrQuiz,
    ScrQuizOptions,
    ScrQuizResult,
    ScrInfo,
    ScrAbout,
    ScrMap,
} Screen;

enum {
    HomeTop = 0, /* "Home" in a context menu: back to the first screen */
    HomeBrowse,
    HomeDetect,
    HomeApply,
    HomeCompare,
    HomeQuizTerm,
    HomeQuizExample,
    HomeInfo,
    HomeAbout,
};

typedef struct {
    uint8_t screen;
    uint8_t filter; /* capability bit for ScrFilter */
    uint16_t cat;
    uint16_t page;
    uint16_t obj;
    uint32_t aux; /* chapter item offset, quiz index */
    uint32_t sel; /* focused target to restore */
    int16_t top; /* scroll to restore */
    uint8_t pick; /* quiz answer given */
} Frame;

struct UiApp {
    UiPlatform platform;
    Page* page;
    char* scratch;

    PkpReader* pack;
    PkpStatus pack_status;
    uint32_t open_ms;
    uint32_t scan_ms;
    uint16_t cap_count[5];
    uint32_t quiz_kind_count[3];

    Frame stack[STACK];
    uint8_t depth;

    uint8_t marks[MAX_MARKERS];
    uint16_t marker_count;
    uint16_t marker_at;
    uint32_t marker_first;

    PkpQuiz quiz;
};

/* ------------------------------------------------------------------ helpers */

static const char* str(UiApp* app, PkpStr s) {
    pkp_string(app->pack, s, app->scratch, SCRATCH);
    return app->scratch;
}

static uint8_t cap_index(uint8_t bit) {
    uint8_t i = 0;
    while(bit > 1) {
        bit >>= 1;
        i++;
    }
    return i;
}

static Frame* top(UiApp* app) {
    return &app->stack[app->depth - 1];
}

static void render(UiApp* app);

static void remember(UiApp* app) {
    top(app)->sel = page_focused_target(app->page);
    top(app)->top = page_scroll(app->page);
}

static void push(UiApp* app, Frame f) {
    remember(app);
    if(app->depth == STACK) {
        memmove(app->stack, app->stack + 1, sizeof(Frame) * (STACK - 1));
        app->depth--;
    }
    app->stack[app->depth++] = f;
    render(app);
}

static uint32_t count_items(UiApp* app, const PkpObject* o, uint8_t type) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o->items_first + k, &it) == PKP_OK && it.type == type) n++;
    }
    return n;
}

static void add_items(UiApp* app, const PkpObject* o, uint8_t type, PageStyle style) {
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o->items_first + k, &it) != PKP_OK || it.type != type) continue;
        page_add(app->page, style, str(app, it.text), 0);
    }
}

/* Links to related objects of relation type `rel` (1-based); `except` inverts the filter. */
static void add_relation_links(UiApp* app, const PkpObject* o, uint8_t rel, bool except) {
    for(uint32_t k = 0; k < o->rel_count; k++) {
        PkpRelation r;
        PkpObject t;
        if(pkp_relation(app->pack, o->rel_first + k, &r) != PKP_OK) continue;
        if((r.type == rel) == except) continue;
        if(pkp_object(app->pack, r.target, &t) != PKP_OK) continue;
        page_add(app->page, PageLink, str(app, t.title), T_OBJECT | r.target);
    }
}

static uint8_t relation_type(UiApp* app, const char* name) {
    char buf[32];
    for(uint32_t i = 1; i <= pkp_count(app->pack, PKP_SEC_RELNAMES); i++) {
        if(pkp_relation_name(app->pack, (uint8_t)i, buf, sizeof(buf)) == PKP_OK &&
           strcmp(buf, name) == 0)
            return (uint8_t)i;
    }
    return 0;
}

static uint16_t quiz_subject(const PkpQuiz* q) {
    return q->kind == PKP_QUIZ_DEFINITION ? q->options[q->answer] : q->options[0];
}

/* The first quiz entry about object `obj`, or -1. Entries are ordered by subject, so a binary
 * search finds the first; a record that cannot be read ends the search. */
static int32_t quiz_for(UiApp* app, uint16_t obj) {
    uint32_t lo = 0, hi = pkp_count(app->pack, PKP_SEC_QUIZ);
    PkpQuiz q;
    while(lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if(pkp_quiz(app->pack, mid, &q) != PKP_OK) break;
        if(quiz_subject(&q) < obj)
            lo = mid + 1;
        else
            hi = mid;
    }
    bool found = lo < pkp_count(app->pack, PKP_SEC_QUIZ) &&
                 pkp_quiz(app->pack, lo, &q) == PKP_OK && quiz_subject(&q) == obj;
    return found ? (int32_t)lo : -1;
}

static int32_t quiz_random(UiApp* app, uint8_t kind) {
    uint32_t n = pkp_count(app->pack, PKP_SEC_QUIZ);
    PkpQuiz q;
    for(int tries = 0; n && tries < 64; tries++) {
        uint32_t i = app->platform.random(app->platform.ctx) % n;
        if(pkp_quiz(app->pack, i, &q) == PKP_OK && q.kind == kind) return (int32_t)i;
    }
    return -1;
}

static void restore(UiApp* app) {
    if(page_block_count(app->page) == 0) {
        /* a list whose every record failed: say so rather than show nothing */
        page_add(app->page, PageNote, "Nothing could be read. Is the SD card in?", 0);
    }
    if(app->depth > 1) page_add_menu(app->page, "Home", T_HOME | HomeTop);
    page_restore(app->page, top(app)->top, top(app)->sel);
}

static const char* num(char* buf, size_t cap, uint32_t n) {
    snprintf(buf, cap, "%lu", (unsigned long)n);
    return buf;
}

/* ------------------------------------------------------------------ lists */

static void render_home(UiApp* app) {
    char n[12];
    page_reset(app->page, "Prismatic Pocket");
    page_set_title_icon(app->page, UiIconAbout);
    if(app->pack_status == PKP_OK) {
        /* each entry with its count and a bar: its share of every object in the pack */
        uint32_t total = pkp_count(app->pack, PKP_SEC_OBJECTS);
        const struct {
            uint8_t icon;
            const char* label;
            uint32_t value;
            uint32_t entry;
        } rows[] = {
            {UiIconBrowse, "Browse", total, HomeBrowse},
            {UiIconDetect, "Detectors", app->cap_count[cap_index(PKP_CAP_DETECT)], HomeDetect},
            {UiIconApply, "What to do", app->cap_count[cap_index(PKP_CAP_APPLY)], HomeApply},
            {UiIconCompare, "Confusables", app->cap_count[cap_index(PKP_CAP_COMPARE)], HomeCompare},
            {UiIconQuiz, "Which term?", app->quiz_kind_count[PKP_QUIZ_DEFINITION], HomeQuizTerm},
            {UiIconTrainer,
             "Manipulative?",
             app->quiz_kind_count[PKP_QUIZ_TRAINER],
             HomeQuizExample},
        };
        for(size_t k = 0; k < sizeof(rows) / sizeof(rows[0]); k++) {
            page_add_row_bar(
                app->page,
                rows[k].icon,
                rows[k].label,
                num(n, sizeof(n), rows[k].value),
                rows[k].value,
                total,
                T_HOME | rows[k].entry);
        }
    } else {
        page_add(app->page, PageNote, "No knowledge pack: see Pack & device.", 0);
    }
    page_add_row(app->page, UiIconInfo, "Pack & device", NULL, T_HOME | HomeInfo);
    page_add_row(app->page, UiIconAbout, "About", NULL, T_HOME | HomeAbout);
    restore(app);
}

static void render_categories(UiApp* app) {
    char n[12];
    page_reset(app->page, "Browse");
    uint32_t largest = 0;
    PkpCategory c;
    for(uint32_t i = 0; i < pkp_count(app->pack, PKP_SEC_CATEGORIES); i++) {
        if(pkp_category(app->pack, i, &c) == PKP_OK && c.count > largest) largest = c.count;
    }
    for(uint32_t i = 0; i < pkp_count(app->pack, PKP_SEC_CATEGORIES); i++) {
        if(pkp_category(app->pack, i, &c) != PKP_OK) continue;
        char label[64];
        snprintf(label, sizeof(label), "%s", str(app, c.name));
        page_add_row_bar(
            app->page,
            UiIconFolder,
            label,
            num(n, sizeof(n), c.count),
            c.count,
            largest,
            T_CATEGORY | i);
    }
    restore(app);
}

static void paging(UiApp* app, uint32_t page, uint32_t pages, bool before) {
    char pos[12];
    if(pages <= 1) return;
    if(before && page > 0) page_add_row(app->page, 0, "< Previous page", NULL, T_PAGE | 0);
    if(!before && page + 1 < pages) page_add_row(app->page, 0, "Next page >", NULL, T_PAGE | 1);
    snprintf(pos, sizeof(pos), "p%lu/%lu", (unsigned long)page + 1, (unsigned long)pages);
    page_set_position(app->page, pos);
}

static void render_objects(UiApp* app) {
    Frame* f = top(app);
    PkpCategory c;
    if(pkp_category(app->pack, f->cat, &c) != PKP_OK) {
        page_reset(app->page, "Browse");
        page_add(app->page, PageNote, "This category could not be read.", 0);
        return;
    }
    page_reset(app->page, str(app, c.name));
    uint32_t pages = (c.count + PAGE_ROWS - 1) / PAGE_ROWS;
    paging(app, f->page, pages, true);
    uint32_t from = (uint32_t)f->page * PAGE_ROWS;
    for(uint32_t i = from; i < c.count && i < from + PAGE_ROWS; i++) {
        PkpObject o;
        if(pkp_object(app->pack, c.first + i, &o) != PKP_OK) continue;
        page_add_row(app->page, 0, str(app, o.title), NULL, T_OBJECT | (c.first + i));
    }
    paging(app, f->page, pages, false);
    restore(app);
}

/* Objects with one capability, paged, in pack order (category, title). */
static void render_filter(UiApp* app) {
    Frame* f = top(app);
    static const char* const names[] = {"All", "Detectors", "What to do", "Quiz", "Confusables"};
    uint32_t total = app->cap_count[cap_index(f->filter)];
    uint32_t pages = (total + PAGE_ROWS - 1) / PAGE_ROWS;
    page_reset(app->page, names[cap_index(f->filter)]);
    paging(app, f->page, pages, true);
    uint32_t skip = (uint32_t)f->page * PAGE_ROWS, shown = 0, seen = 0;
    for(uint32_t i = 0; i < pkp_count(app->pack, PKP_SEC_OBJECTS) && shown < PAGE_ROWS; i++) {
        PkpObject o;
        if(pkp_object(app->pack, i, &o) != PKP_OK || !(o.caps & f->filter)) continue;
        if(seen++ < skip) continue;
        page_add_row(app->page, 0, str(app, o.title), NULL, T_OBJECT | i);
        shown++;
    }
    paging(app, f->page, pages, false);
    restore(app);
}

/* ------------------------------------------------------------------ documents */

static void section(UiApp* app, const PkpObject* o, uint8_t type, const char* heading) {
    /* every section carries the icon of its role */
    static const uint8_t icons[] = {
        [PKP_ITEM_INDICATOR] = UiIconDetect,
        [PKP_ITEM_ACTION] = UiIconApply,
        [PKP_ITEM_ESCALATION] = UiIconAlert,
        [PKP_ITEM_ALTERNATIVE] = UiIconCompare,
        [PKP_ITEM_VALIDATION] = UiIconCorrect,
    };
    if(!count_items(app, o, type)) return;
    page_add_icon(app->page, PageHeading, icons[type], heading, 0);
    add_items(app, o, type, PageBullet);
}

static void render_article(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK) {
        page_reset(app->page, "Article");
        page_add(app->page, PageNote, "This object could not be read.", 0);
        return;
    }
    char kind[24], line[96];
    PkpCategory c;
    page_reset(app->page, "Article");
    if(pkp_category(app->pack, o.category, &c) == PKP_OK) page_reset(app->page, str(app, c.name));
    page_add(app->page, PageTitle, str(app, o.title), 0);
    pkp_kind_name(app->pack, o.kind, kind, sizeof(kind));
    snprintf(line, sizeof(line), "%s\t%s", kind, o.code.len ? str(app, o.code) : "");
    page_add(app->page, PageBadge, line, 0);
    /* the actions first, as chips: one Down reaches them, however long the definition */
    uint32_t markers = count_items(app, &o, PKP_ITEM_INDICATOR);
    if(markers) {
        snprintf(line, sizeof(line), "%lu", (unsigned long)markers);
        page_add_icon(app->page, PageChip, UiIconDetect, line, T_DETECT);
    }
    if(o.caps & PKP_CAP_QUIZ) page_add_icon(app->page, PageChip, UiIconQuiz, "Quiz", T_QUIZ);
    if(o.rel_count) {
        snprintf(line, sizeof(line), "%u", o.rel_count);
        page_add_icon(app->page, PageChip, UiIconMap, line, T_MAP | f->obj);
    }
    page_add(app->page, PageParagraph, str(app, o.definition), 0);

    section(app, &o, PKP_ITEM_INDICATOR, "How to recognise it");
    section(app, &o, PKP_ITEM_ACTION, "What to do");
    section(app, &o, PKP_ITEM_ESCALATION, "When to escalate");
    uint8_t confused = relation_type(app, "confused_with");
    bool alt = count_items(app, &o, PKP_ITEM_ALTERNATIVE) > 0;
    bool has_confused = false;
    for(uint32_t k = 0; confused && k < o.rel_count; k++) {
        PkpRelation r;
        if(pkp_relation(app->pack, o.rel_first + k, &r) == PKP_OK && r.type == confused)
            has_confused = true;
    }
    if(alt || has_confused) {
        page_add_icon(app->page, PageHeading, UiIconCompare, "Don't confuse with", 0);
        add_items(app, &o, PKP_ITEM_ALTERNATIVE, PageBullet);
        add_relation_links(app, &o, confused, false);
    }
    section(app, &o, PKP_ITEM_VALIDATION, "Validation tests");
    if(count_items(app, &o, PKP_ITEM_HEADING)) {
        page_add_icon(app->page, PageHeading, UiIconArticle, "Chapters", 0);
        for(uint32_t k = 0; k < o.items_count; k++) {
            PkpItem it;
            if(pkp_item(app->pack, o.items_first + k, &it) == PKP_OK &&
               it.type == PKP_ITEM_HEADING) {
                page_add(app->page, PageLink, str(app, it.text), T_CHAPTER | k);
            }
        }
    }
    if(o.rel_count > 0 && (!has_confused || o.rel_count > 1)) {
        page_add_icon(app->page, PageHeading, UiIconMap, "See also", 0);
        page_add(app->page, PageLink, "Map of links", T_MAP | f->obj);
        add_relation_links(app, &o, confused, true);
    }
    if(pkp_category(app->pack, o.category, &c) == PKP_OK) {
        snprintf(line, sizeof(line), "In: %s", str(app, c.name));
        page_add(app->page, PageLink, line, T_CATEGORY | o.category);
    }
    page_add(app->page, PageNote, "Markers describe; they never prove.", 0);
    if(markers) page_add_menu(app->page, "Run the detector", T_DETECT);
    if(o.caps & PKP_CAP_QUIZ) page_add_menu(app->page, "Take the quiz", T_QUIZ);
    if(o.rel_count) page_add_menu(app->page, "Map of links", T_MAP | f->obj);
    page_add_menu(app->page, "Open the category", T_CATEGORY | o.category);
    restore(app);
}

/* The local map: the object in the middle, its links around it, confusables dotted. */
static void render_map(UiApp* app) {
    Frame* f = top(app);
    PkpObject o, t;
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK) {
        page_reset(app->page, "Map");
        page_add(app->page, PageNote, "This object could not be read.", 0);
        return;
    }
    page_reset(app->page, str(app, o.title));
    page_set_graph(app->page, str(app, o.title), T_OBJECT | f->obj);
    uint8_t confused = relation_type(app, "confused_with");
    for(int pass = 0; pass < 2; pass++) {
        for(uint32_t k = 0; k < o.rel_count; k++) {
            PkpRelation r;
            if(pkp_relation(app->pack, o.rel_first + k, &r) != PKP_OK) continue;
            if((r.type == confused) != (pass == 0)) continue;
            if(pkp_object(app->pack, r.target, &t) != PKP_OK) continue;
            page_add_node(app->page, str(app, t.title), pass == 0 ? 1 : 0, T_MAP | r.target);
        }
    }
}

static void render_chapter(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    PkpItem it;
    page_reset(app->page, "Chapter");
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK ||
       pkp_item(app->pack, o.items_first + f->aux, &it) != PKP_OK) {
        page_add(app->page, PageNote, "This chapter could not be read.", 0);
        return;
    }
    page_reset(app->page, str(app, o.title));
    page_add(app->page, PageTitle, str(app, it.text), 0);
    uint32_t k = f->aux + 1;
    for(; k < o.items_count; k++) {
        if(pkp_item(app->pack, o.items_first + k, &it) != PKP_OK || it.type != PKP_ITEM_TEXT)
            break;
        page_add(app->page, PageParagraph, str(app, it.text), 0);
    }
    for(; k < o.items_count; k++) {
        if(pkp_item(app->pack, o.items_first + k, &it) == PKP_OK && it.type == PKP_ITEM_HEADING) {
            char label[64];
            snprintf(label, sizeof(label), "Next: %s", str(app, it.text));
            page_add(app->page, PageLink, label, T_CHAPTER | k);
            break;
        }
    }
    restore(app);
}

static void render_detect(UiApp* app) {
    PkpItem it;
    PkpObject o;
    char line[24];
    if(pkp_item(app->pack, app->marker_first + app->marker_at, &it) != PKP_OK ||
       pkp_object(app->pack, top(app)->obj, &o) != PKP_OK) {
        page_reset(app->page, "Detector");
        page_add(app->page, PageNote, "This marker could not be read.", 0);
        return;
    }
    page_reset(app->page, str(app, o.title));
    snprintf(line, sizeof(line), "%u/%u", app->marker_at + 1, app->marker_count);
    page_set_position(app->page, line);
    snprintf(line, sizeof(line), "%u/%u", app->marker_at, app->marker_count);
    page_add(app->page, PageSteps, line, 0);
    page_add(app->page, PageParagraph, str(app, it.text), 0);
    page_add(app->page, PageMeta, "Hold OK: does not apply", 0);
    page_set_softkeys(app->page, "No", "?", "Yes");
}

static void render_detect_result(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    page_reset(app->page, "Your observations");
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK) {
        page_add(app->page, PageNote, "This object could not be read.", 0);
        return;
    }
    PocketSummary s;
    pocket_summarize(app->marks, app->marker_count, &s);
    char line[64];
    snprintf(line, sizeof(line), "%u/%u markers observed", s.counts[POCKET_OBSERVED], s.total);
    page_add(app->page, PageBig, line, 0);
    snprintf(
        line,
        sizeof(line),
        "%u seen,%u no,%u ?,%u n/a",
        s.counts[POCKET_OBSERVED],
        s.counts[POCKET_NOT_OBSERVED],
        s.counts[POCKET_UNKNOWN],
        s.counts[POCKET_NOT_APPLICABLE]);
    page_add(app->page, PageBar, line, 0);
    page_add(app->page, PageNote, "Your marks only: not a score, not a diagnosis of anyone.", 0);
    page_add(app->page, PageLink, str(app, o.title), T_OBJECT | f->obj);
    uint8_t confused = relation_type(app, "confused_with");
    if(count_items(app, &o, PKP_ITEM_ALTERNATIVE)) {
        page_add(app->page, PageHeading, "Rule out first", 0);
        add_items(app, &o, PKP_ITEM_ALTERNATIVE, PageBullet);
        add_relation_links(app, &o, confused, false);
    }
    section(app, &o, PKP_ITEM_VALIDATION, "Validate");
    section(app, &o, PKP_ITEM_ACTION, "Next steps");
    section(app, &o, PKP_ITEM_ESCALATION, "When to escalate");
    restore(app);
}

static void render_quiz(UiApp* app) {
    page_reset(app->page, "Quiz");
    if(pkp_quiz(app->pack, top(app)->aux, &app->quiz) != PKP_OK) {
        page_add(app->page, PageNote, "This question could not be read.", 0);
        return;
    }
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        page_reset(app->page, "Manipulative?");
        page_set_title_icon(app->page, UiIconTrainer);
        page_add(app->page, PageMeta, str(app, app->quiz.context), 0);
        page_add(app->page, PageQuote, str(app, app->quiz.prompt), 0);
        page_set_softkeys(app->page, "Manip.", NULL, "Benign");
    } else {
        page_reset(app->page, "Which term?");
        page_set_title_icon(app->page, UiIconQuiz);
        page_add(app->page, PageParagraph, str(app, app->quiz.prompt), 0);
        page_set_softkeys(app->page, NULL, "Choose", NULL);
    }
}

static void render_quiz_options(UiApp* app) {
    page_reset(app->page, "Which term?");
    page_set_title_icon(app->page, UiIconQuiz);
    for(uint8_t i = 0; i < 4; i++) {
        PkpObject o;
        if(pkp_object(app->pack, app->quiz.options[i], &o) != PKP_OK) continue;
        page_add_row(app->page, 0, str(app, o.title), NULL, T_OPTION | i);
    }
}

static void render_quiz_result(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    page_reset(app->page, "Quiz");
    if(pkp_quiz(app->pack, f->aux, &app->quiz) != PKP_OK) {
        page_add(app->page, PageNote, "This question could not be read.", 0);
        return;
    }
    uint16_t subject = quiz_subject(&app->quiz);
    bool have = pkp_object(app->pack, subject, &o) == PKP_OK;
    bool ok = pocket_quiz_correct(&app->quiz, f->pick);
    page_reset(app->page, ok ? "Correct" : "Not quite");
    page_set_title_icon(app->page, ok ? UiIconCorrect : UiIconWrong);
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        page_add(
            app->page,
            PageMeta,
            app->quiz.answer ? "This example is benign." : "This example is manipulative.",
            0);
        page_add(app->page, PageQuote, str(app, app->quiz.prompt), 0);
        page_add(app->page, PageHeading, "Why", 0);
        page_add(app->page, PageParagraph, str(app, app->quiz.explanation), 0);
        page_add(app->page, PageHeading, "A good response", 0);
        page_add(app->page, PageParagraph, str(app, app->quiz.response), 0);
    } else if(have) {
        page_add(app->page, PageMeta, "The term was", 0);
        page_add(app->page, PageTitle, str(app, o.title), 0);
        page_add(app->page, PageNote, str(app, o.definition), 0);
    }
    page_add(app->page, PageLink, "Next question", T_NEXT_QUIZ | app->quiz.kind);
    if(have) page_add(app->page, PageLink, str(app, o.title), T_OBJECT | subject);
    restore(app);
}

static void render_info(UiApp* app) {
    char line[96];
    page_reset(app->page, "Pack & device");
    page_set_title_icon(app->page, UiIconInfo);
    if(app->pack_status != PKP_OK) {
        page_add(app->page, PageHeading, "No usable pack", 0);
        page_add(
            app->page,
            PageParagraph,
            app->pack_status == PKP_E_MISSING ? "None found." : pkp_status_text(app->pack_status),
            0);
        page_add(
            app->page,
            PageNote,
            "Copy a .pkp file to SD/apps_data/prismatic/ (computer: just deploy-pack).",
            0);
    } else {
        static const char* const labels[PKP_META_COUNT] = {
            "Pack", "Title", "Version", "Source", "Commit", "Built by", "Licence"};
        PkpStr s;
        page_add(app->page, PageHeading, "Pack", 0);
        for(uint32_t i = 1; i < PKP_META_COUNT; i++) {
            if(pkp_meta(app->pack, i, &s) == PKP_OK) {
                snprintf(line, sizeof(line), "%s: %s", labels[i], str(app, s));
                page_add(app->page, PageBullet, line, 0);
            }
        }
        page_add(app->page, PageHeading, "Contents", 0);
        snprintf(
            line,
            sizeof(line),
            "%lu objects in %lu categories",
            (unsigned long)pkp_count(app->pack, PKP_SEC_OBJECTS),
            (unsigned long)pkp_count(app->pack, PKP_SEC_CATEGORIES));
        page_add(app->page, PageBullet, line, 0);
        snprintf(
            line,
            sizeof(line),
            "%lu items, %lu links, %lu questions",
            (unsigned long)pkp_count(app->pack, PKP_SEC_ITEMS),
            (unsigned long)pkp_count(app->pack, PKP_SEC_RELATIONS),
            (unsigned long)pkp_count(app->pack, PKP_SEC_QUIZ));
        page_add(app->page, PageBullet, line, 0);
        page_add(app->page, PageHeading, "Measured here", 0);
        snprintf(line, sizeof(line), "Open + index CRC: %lu ms", (unsigned long)app->open_ms);
        page_add(app->page, PageBullet, line, 0);
        snprintf(line, sizeof(line), "Index scan: %lu ms", (unsigned long)app->scan_ms);
        page_add(app->page, PageBullet, line, 0);
        snprintf(line, sizeof(line), "Pack size: %lu B", (unsigned long)app->pack->size);
        page_add(app->page, PageBullet, line, 0);
        const PkpCache* c = app->platform.cache;
        uint32_t asked = c->hits + c->misses;
        page_add(app->page, PageHeading, "Read cache", 0);
        snprintf(
            line,
            sizeof(line),
            "%u blocks of %u B; %lu of %lu reads served from memory",
            PKP_CACHE_BLOCKS,
            PKP_CACHE_BLOCK,
            (unsigned long)c->hits,
            (unsigned long)asked);
        page_add(app->page, PageParagraph, line, 0);
        snprintf(
            line,
            sizeof(line),
            "%lu hits,%lu misses",
            (unsigned long)c->hits,
            (unsigned long)c->misses);
        page_add(app->page, PageBar, line, 0);
    }
    uint32_t free_b = 0, largest = 0;
    app->platform.heap(app->platform.ctx, &free_b, &largest);
    snprintf(
        line,
        sizeof(line),
        "Free heap: %lu B (largest %lu B)",
        (unsigned long)free_b,
        (unsigned long)largest);
    page_add(app->page, PageBullet, line, 0);
    snprintf(line, sizeof(line), "App v%s", PRISMATIC_VERSION);
    page_add(app->page, PageBullet, line, 0);
    restore(app);
}

static void render_about(UiApp* app) {
    char line[48];
    page_reset(app->page, "About");
    page_set_title_icon(app->page, UiIconAbout);
    page_add(app->page, PageTitle, "Prismatic Pocket", 0);
    snprintf(line, sizeof(line), "v%s - offline - evidence-first", PRISMATIC_VERSION);
    page_add(app->page, PageMeta, line, 0);
    page_add(
        app->page,
        PageParagraph,
        "Every screen comes from the knowledge pack on the SD card: articles, detectors, "
        "what to do, confusables and quizzes.",
        0);
    page_add(app->page, PageHeading, "Keys", 0);
    page_add(app->page, PageBullet, "Up/Down: next link, else scroll", 0);
    page_add(app->page, PageBullet, "Left/Right: previous/next section", 0);
    page_add(app->page, PageBullet, "Hold Up/Down: same as Left/Right", 0);
    page_add(app->page, PageBullet, "OK: open; Back: one step back", 0);
    page_add(app->page, PageBullet, "Hold OK: actions, contents, map", 0);
    page_add(
        app->page,
        PageNote,
        "The detector counts what you mark. It does not score, rank or diagnose anyone.",
        0);
    page_add(app->page, PageMeta, "github.com/korczis/prismatic-flipperzero", 0);
    restore(app);
}

static void render(UiApp* app) {
    switch(top(app)->screen) {
    case ScrHome:
        render_home(app);
        break;
    case ScrCategories:
        render_categories(app);
        break;
    case ScrObjects:
        render_objects(app);
        break;
    case ScrFilter:
        render_filter(app);
        break;
    case ScrArticle:
        render_article(app);
        break;
    case ScrChapter:
        render_chapter(app);
        break;
    case ScrDetect:
        render_detect(app);
        break;
    case ScrDetectResult:
        render_detect_result(app);
        break;
    case ScrQuiz:
        render_quiz(app);
        break;
    case ScrQuizOptions:
        render_quiz_options(app);
        break;
    case ScrQuizResult:
        render_quiz_result(app);
        break;
    case ScrInfo:
        render_info(app);
        break;
    case ScrMap:
        render_map(app);
        break;
    default:
        render_about(app);
        break;
    }
}

/* ------------------------------------------------------------------ actions */

static void start_detect(UiApp* app, uint16_t obj) {
    PkpObject o;
    app->marker_count = 0;
    app->marker_at = 0;
    o.items_count = 0;
    if(pkp_object(app->pack, obj, &o) != PKP_OK) o.items_count = 0;
    for(uint32_t k = 0; k < o.items_count && app->marker_count < MAX_MARKERS; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o.items_first + k, &it) != PKP_OK) break;
        if(it.type == PKP_ITEM_INDICATOR) {
            if(app->marker_count == 0) app->marker_first = o.items_first + k;
            app->marks[app->marker_count++] = POCKET_UNANSWERED;
        } else if(app->marker_count) {
            break; /* indicators are contiguous: the encoder writes them first */
        }
    }
    if(app->marker_count == 0) app->marker_first = UINT32_MAX; /* the screen says why */
    push(app, (Frame){.screen = ScrDetect, .obj = obj});
}

/* q < 0 (no readable question) opens the quiz screen anyway, which says it could not read one. */
static void start_quiz(UiApp* app, int32_t q, uint16_t obj) {
    push(app, (Frame){.screen = ScrQuiz, .aux = q < 0 ? UINT32_MAX : (uint32_t)q, .obj = obj});
}

static void home(UiApp* app, uint32_t entry) {
    static const uint8_t filters[] = {0, 0, PKP_CAP_DETECT, PKP_CAP_APPLY, PKP_CAP_COMPARE};
    if(entry == HomeTop) {
        app->depth = 1;
        top(app)->sel = 0;
        top(app)->top = 0;
        render(app);
    } else if(entry == HomeBrowse)
        push(app, (Frame){.screen = ScrCategories});
    else if(entry <= HomeCompare)
        push(app, (Frame){.screen = ScrFilter, .filter = filters[entry]});
    else if(entry == HomeInfo)
        push(app, (Frame){.screen = ScrInfo});
    else if(entry == HomeAbout)
        push(app, (Frame){.screen = ScrAbout});
    else
        start_quiz(
            app,
            quiz_random(app, entry == HomeQuizTerm ? PKP_QUIZ_DEFINITION : PKP_QUIZ_TRAINER),
            0);
}

static void open_target(UiApp* app, uint32_t target, bool long_press) {
    Frame* f = top(app);
    switch(T_KIND(target)) {
    case T_MAP:
        /* OK walks the map to that object; holding OK opens its article */
        push(
            app,
            (Frame){.screen = long_press ? ScrArticle : ScrMap, .obj = (uint16_t)T_ARG(target)});
        break;
    case T_HOME:
        home(app, T_ARG(target));
        break;
    case T_OBJECT:
        push(app, (Frame){.screen = ScrArticle, .obj = (uint16_t)T_ARG(target)});
        break;
    case T_CHAPTER:
        push(app, (Frame){.screen = ScrChapter, .obj = f->obj, .aux = T_ARG(target)});
        break;
    case T_DETECT:
        start_detect(app, f->obj);
        break;
    case T_QUIZ:
        start_quiz(app, quiz_for(app, f->obj), f->obj);
        break;
    case T_CATEGORY:
        push(app, (Frame){.screen = ScrObjects, .cat = (uint16_t)T_ARG(target)});
        break;
    case T_PAGE:
        f->page = (uint16_t)(f->page + (T_ARG(target) ? 1 : -1));
        f->sel = 0;
        f->top = 0;
        render(app);
        break;
    case T_OPTION:
        app->depth--; /* the result replaces the question and its options */
        top(app)->screen = ScrQuizResult;
        top(app)->pick = (uint8_t)T_ARG(target);
        top(app)->sel = 0;
        top(app)->top = 0;
        render(app);
        break;
    case T_NEXT_QUIZ:
        app->depth--; /* replace the finished question */
        start_quiz(app, quiz_random(app, (uint8_t)T_ARG(target)), 0);
        break;
    }
}

static void softkey(UiApp* app, uint32_t slot, bool long_press) {
    Frame* f = top(app);
    if(f->screen == ScrDetect) {
        static const uint8_t marks[3] = {POCKET_NOT_OBSERVED, POCKET_UNKNOWN, POCKET_OBSERVED};
        app->marks[app->marker_at] = long_press ? POCKET_NOT_APPLICABLE : marks[slot];
        if(++app->marker_at < app->marker_count) {
            render_detect(app);
        } else {
            f->screen = ScrDetectResult; /* the result replaces the questions */
            render(app);
        }
    } else if(f->screen == ScrQuiz && !long_press) {
        if(app->quiz.kind == PKP_QUIZ_TRAINER) {
            f->screen = ScrQuizResult;
            f->pick = slot == 0 ? 0 : 1;
            render(app);
        } else {
            push(app, (Frame){.screen = ScrQuizOptions, .aux = f->aux, .obj = f->obj});
        }
    }
}

/* ------------------------------------------------------------------ lifecycle */

UiApp* ui_app_alloc(const UiPlatform* platform) {
    UiApp* app = calloc(1, sizeof(UiApp));
    app->platform = *platform;
    app->page = page_alloc();
    app->scratch = malloc(SCRATCH);
    app->pack_status = PKP_E_MISSING;
    app->stack[0] = (Frame){.screen = ScrHome};
    app->depth = 1;
    return app;
}

void ui_app_free(UiApp* app) {
    page_free(app->page);
    free(app->scratch);
    free(app);
}

void ui_app_loading(UiApp* app) {
    page_reset(app->page, "Prismatic Pocket");
    page_set_title_icon(app->page, UiIconAbout);
    page_add(app->page, PageParagraph, "Opening the knowledge pack...", 0);
    page_add(app->page, PageMeta, "Checking its index", 0);
}

void ui_app_set_pack(UiApp* app, PkpReader* pack, PkpStatus status, uint32_t open_ms) {
    app->pack = pack;
    app->pack_status = status;
    app->open_ms = open_ms;
    if(status == PKP_OK) {
        /* one pass over the object index for the home counts (sequential, cached reads) */
        uint32_t t0 = app->platform.tick_ms(app->platform.ctx);
        for(uint32_t i = 0; i < pkp_count(pack, PKP_SEC_OBJECTS); i++) {
            PkpObject o;
            if(pkp_object(pack, i, &o) != PKP_OK) continue;
            for(uint8_t bit = 1; bit <= PKP_CAP_COMPARE; bit <<= 1) {
                if(o.caps & bit) app->cap_count[cap_index(bit)]++;
            }
        }
        for(uint32_t i = 0; i < pkp_count(pack, PKP_SEC_QUIZ); i++) {
            PkpQuiz q;
            if(pkp_quiz(pack, i, &q) == PKP_OK && q.kind < 3) app->quiz_kind_count[q.kind]++;
        }
        app->scan_ms = app->platform.tick_ms(app->platform.ctx) - t0;
    }
    app->depth = 1;
    app->stack[0] = (Frame){.screen = ScrHome};
    render(app);
}

void ui_app_draw(UiApp* app, Canvas* canvas) {
    page_draw(app->page, canvas);
}

bool ui_app_input(UiApp* app, PageKey key, PagePress press) {
    PageAction act = page_input(app->page, key, press);
    switch(act.kind) {
    case PageActBack:
        if(app->depth <= 1) return false;
        app->depth--;
        render(app);
        break;
    case PageActOpen:
        open_target(app, act.target, act.long_press);
        break;
    case PageActSoftkey:
        softkey(app, act.target, act.long_press);
        break;
    default:
        break;
    }
    return true;
}

Page* ui_app_page(UiApp* app) {
    return app->page;
}
