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
#define T_OF_KIND   (11u << 24) /* argument: object kind */
#define T_PICK      (12u << 24) /* argument: object whose look-alikes to list */
#define T_COMPARE   (13u << 24) /* argument: the look-alike to compare the frame's object with */
#define KIND_CAP    32u
#define T_KIND(t)   ((t) & 0xFF000000u)
#define T_ARG(t)    ((t) & 0x00FFFFFFu)

/* Every screen, once: the enum and the names (tests, the screen atlas) both come from here. */
#define SCREENS(X)  \
    X(Home)         \
    X(Categories)   \
    X(Objects)      \
    X(Filter)       \
    X(Article)      \
    X(Chapter)      \
    X(Detect)       \
    X(DetectResult) \
    X(Quiz)         \
    X(QuizOptions)  \
    X(QuizResult)   \
    X(Info)         \
    X(About)        \
    X(Map)          \
    X(Hub)          \
    X(Kinds)        \
    X(Forget)       \
    X(Compare)

typedef enum {
#define SCREEN_ENUM(name) Scr##name,
    SCREENS(SCREEN_ENUM)
#undef SCREEN_ENUM
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
    HomeKinds,
    HomeAll,
    HomeQuizzable,
    HomeRandom,
    HomeRandomDetect,
    HomeReview,
    HomeForget,
    HomeForgetNow,
    HomeHub, /* HomeHub + n: hub n of the hubs table */
};

typedef struct {
    uint8_t screen;
    uint8_t filter; /* capability bit for ScrFilter; 0: objects of kind `cat` */
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
    uint16_t kind_count[KIND_CAP];

    Frame stack[STACK];
    uint8_t depth;

    uint8_t marks[MAX_MARKERS];
    uint16_t marker_count;
    uint16_t marker_at;
    /* the detector's candidates: one object, or two side by side; their markers in a row */
    uint8_t cands;
    uint16_t cand_obj[2];
    uint32_t cand_first[2];
    uint16_t cand_count[2];

    PkpQuiz quiz;
    uint16_t quiz_asked; /* this session's answers, shown in the quiz header */
    uint16_t quiz_right;
    Progress progress; /* Leitner boxes, loaded from and saved to storage */
    uint8_t* file; /* PROGRESS_MAX_FILE bytes for reading and writing it */
    bool progress_ok; /* the last save succeeded */
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

/* What one home entry leads to, counted from the pack: zero hides the entry. */
static uint32_t entry_count(UiApp* app, uint32_t entry) {
    uint32_t total = pkp_count(app->pack, PKP_SEC_OBJECTS), kinds = 0;
    for(uint32_t k = 0; k < KIND_CAP; k++)
        kinds += app->kind_count[k] != 0;
    const uint32_t counts[] = {
        [HomeBrowse] = pkp_count(app->pack, PKP_SEC_CATEGORIES),
        [HomeDetect] = app->cap_count[cap_index(PKP_CAP_DETECT)],
        [HomeApply] = app->cap_count[cap_index(PKP_CAP_APPLY)],
        [HomeCompare] = app->cap_count[cap_index(PKP_CAP_COMPARE)],
        [HomeQuizTerm] = app->quiz_kind_count[PKP_QUIZ_DEFINITION],
        [HomeQuizExample] = app->quiz_kind_count[PKP_QUIZ_TRAINER],
        [HomeKinds] = kinds,
        [HomeAll] = total,
        [HomeQuizzable] = app->cap_count[cap_index(PKP_CAP_QUIZ)],
        [HomeRandom] = total,
        [HomeRandomDetect] = app->cap_count[cap_index(PKP_CAP_DETECT)],
        [HomeReview] = progress_due_count(&app->progress),
    };
    return entry < sizeof(counts) / sizeof(counts[0]) ? counts[entry] : 0;
}

/* The information architecture: four hubs, each a short menu of entries. Labels and hints are
 * the only words here; every count comes from the pack, and an entry with nothing behind it is
 * left out. */
typedef struct {
    uint8_t icon;
    uint32_t entry;
    const char* label;
    const char* unit; /* the count's format, "%lu"; "" for none */
    const char* hint;
    uint8_t share; /* a bar of the count's share: ShareNone, ShareObjects, ShareQuestions */
} HubEntry;

enum {
    ShareNone,
    ShareObjects,
    ShareQuestions,
};

typedef struct {
    uint8_t icon;
    const char* name;
    uint32_t count_entry; /* the count its home tile shows */
    const char* hint; /* the status line: a format of the tile's count */
    HubEntry entries[5];
} Hub;

static const Hub hubs[] = {
    {UiIconBrowse,
     "Library",
     HomeAll,
     "%lu objects to read",
     {{UiIconFolder, HomeBrowse, "By chapter", "%lu", "Chapters in book order", ShareNone},
      {UiIconArticle, HomeKinds, "By kind", "%lu", "One list per kind", ShareNone},
      {UiIconBrowse, HomeAll, "Everything", "%lu", "All, in pack order", ShareObjects},
      {UiIconMap, HomeRandom, "Surprise me", "", "A random article", ShareNone}}},
    {UiIconDetect,
     "Detect",
     HomeDetect,
     "%lu: mark what you saw",
     {{UiIconDetect, HomeDetect, "Detectors", "%lu", "Objects with markers", ShareObjects},
      {UiIconAlert, HomeRandomDetect, "Random detector", "", "Markers of a random one", ShareNone}}},
    {UiIconApply,
     "Act",
     HomeApply,
     "%lu with steps to take",
     {{UiIconApply, HomeApply, "What to do", "%lu", "Objects with steps", ShareObjects},
      {UiIconCompare, HomeCompare, "Confusables", "%lu", "Pairs to tell apart", ShareObjects}}},
    {UiIconQuiz,
     "Train",
     HomeQuizTerm,
     "%lu questions to try",
     {{UiIconQuiz, HomeQuizTerm, "Which term?", "%lu", "Pick the term of four", ShareQuestions},
      {UiIconTrainer,
       HomeQuizExample,
       "Manipulative?",
       "%lu",
       "Manipulative or benign",
       ShareQuestions},
      {UiIconCorrect, HomeReview, "Review", "%lu", "Due again: boxes 1 to 5", ShareNone},
      {UiIconArticle,
       HomeQuizzable,
       "Quiz an object",
       "%lu",
       "Quiz a chosen object",
       ShareObjects}}},
};
#define HUB_COUNT (sizeof(hubs) / sizeof(hubs[0]))

static void render_home(UiApp* app) {
    char count[12], hint[64];
    page_reset(app->page, "Prismatic Pocket");
    page_set_title_icon(app->page, UiIconAbout);
    if(app->pack_status == PKP_OK) {
        for(uint32_t h = 0; h < HUB_COUNT; h++) {
            uint32_t n = entry_count(app, hubs[h].count_entry);
            page_add_tile(
                app->page,
                hubs[h].icon,
                hubs[h].name,
                num(count, sizeof(count), n),
                T_HOME | (HomeHub + h));
            snprintf(hint, sizeof(hint), hubs[h].hint, (unsigned long)n);
            page_set_hint(app->page, hint);
        }
    } else {
        page_add(app->page, PageNote, "No knowledge pack yet. Info says how to add one.", 0);
    }
    page_add_tile(app->page, UiIconInfo, "Info", "device", T_HOME | HomeInfo);
    page_set_hint(app->page, "Pack, cache, memory");
    page_add_tile(app->page, UiIconAbout, "About", "v" PRISMATIC_VERSION, T_HOME | HomeAbout);
    page_set_hint(app->page, "Keys; hold Back: Home");
    restore(app);
}

static void render_hub(UiApp* app) {
    const Hub* h = &hubs[top(app)->aux];
    char right[16];
    const uint32_t of[] = {
        [ShareNone] = 0,
        [ShareObjects] = pkp_count(app->pack, PKP_SEC_OBJECTS),
        [ShareQuestions] = pkp_count(app->pack, PKP_SEC_QUIZ),
    };
    page_reset(app->page, h->name);
    page_set_title_icon(app->page, h->icon);
    for(int e = 0; e < 5 && h->entries[e].label; e++) {
        const HubEntry* he = &h->entries[e];
        uint32_t n = entry_count(app, he->entry);
        if(n == 0) continue;
        snprintf(right, sizeof(right), he->unit, (unsigned long)n);
        page_add_row_bar(
            app->page, he->icon, he->label, right, n, of[he->share], T_HOME | he->entry);
        page_set_hint(app->page, he->hint);
    }
    restore(app);
}

/* Every kind of object in the pack, with how many there are. */
static void render_kinds(UiApp* app) {
    char n[12], name[24];
    uint32_t largest = 0, kinds = pkp_count(app->pack, PKP_SEC_KINDS);
    page_reset(app->page, "By kind");
    page_set_title_icon(app->page, UiIconArticle);
    for(uint32_t k = 0; k < KIND_CAP; k++)
        if(app->kind_count[k] > largest) largest = app->kind_count[k];
    for(uint32_t k = 1; k <= kinds && k < KIND_CAP; k++) { /* kinds count from 1 */
        if(app->kind_count[k] == 0) continue;
        pkp_kind_name(app->pack, (uint8_t)k, name, sizeof(name));
        page_add_row_bar(
            app->page,
            UiIconArticle,
            name,
            num(n, sizeof(n), app->kind_count[k]),
            app->kind_count[k],
            largest,
            T_OF_KIND | k);
    }
    restore(app);
}

static void render_categories(UiApp* app) {
    char n[12];
    page_reset(app->page, "Chapters");
    page_set_title_icon(app->page, UiIconFolder);
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
    char pos[24]; /* "p" and two 32-bit counts */
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
        page_reset(app->page, "Chapters");
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
    static const char* const names[] = {
        "Everything", "Detectors", "What to do", "Quiz", "Confusables"};
    char kind[24];
    bool by_kind = f->filter == 0;
    if(by_kind) pkp_kind_name(app->pack, (uint8_t)f->cat, kind, sizeof(kind));
    uint32_t total = by_kind ? app->kind_count[f->cat] : app->cap_count[cap_index(f->filter)];
    uint32_t pages = (total + PAGE_ROWS - 1) / PAGE_ROWS;
    page_reset(app->page, by_kind ? kind : names[cap_index(f->filter)]);
    paging(app, f->page, pages, true);
    uint32_t skip = (uint32_t)f->page * PAGE_ROWS, shown = 0, seen = 0;
    for(uint32_t i = 0; i < pkp_count(app->pack, PKP_SEC_OBJECTS) && shown < PAGE_ROWS; i++) {
        PkpObject o;
        if(pkp_object(app->pack, i, &o) != PKP_OK) continue;
        if(by_kind ? o.kind != f->cat : !(o.caps & f->filter)) continue;
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

/* The objects `o` is confused with that have markers of their own; adds them as rows when
 * `page` is set. Returns how many there are. */
static uint32_t look_alikes(UiApp* app, const PkpObject* o, Page* page) {
    uint8_t confused = relation_type(app, "confused_with");
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->rel_count; k++) {
        PkpRelation r;
        PkpObject t;
        if(pkp_relation(app->pack, o->rel_first + k, &r) != PKP_OK || r.type != confused) continue;
        if(pkp_object(app->pack, r.target, &t) != PKP_OK || !(t.caps & PKP_CAP_DETECT)) continue;
        if(page) page_add_row(page, UiIconCompare, str(app, t.title), NULL, T_COMPARE | r.target);
        n++;
    }
    return n;
}

/* Pick the look-alike to run side by side with the object. */
static void render_compare(UiApp* app) {
    PkpObject o;
    page_reset(app->page, "Compare with");
    page_set_title_icon(app->page, UiIconCompare);
    if(pkp_object(app->pack, top(app)->obj, &o) == PKP_OK) look_alikes(app, &o, app->page);
    restore(app);
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
    if(markers && look_alikes(app, &o, NULL))
        page_add_menu(app->page, "Compare with a look-alike", T_PICK | f->obj);
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
    /* which candidate the current marker belongs to */
    uint8_t k = app->cands == 2 && app->marker_at >= app->cand_count[0];
    uint32_t at = app->marker_at - (k ? app->cand_count[0] : 0u);
    if(app->marker_count == 0 || pkp_item(app->pack, app->cand_first[k] + at, &it) != PKP_OK ||
       pkp_object(app->pack, app->cand_obj[k], &o) != PKP_OK) {
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
    page_set_softkeys(app->page, "No", "Unsure", "Yes");
}

/* Two candidates' marks side by side: a count and a bar each, never a ranking. */
static void render_compare_result(UiApp* app) {
    char line[64];
    page_reset(app->page, "Side by side");
    page_set_title_icon(app->page, UiIconCompare);
    for(uint8_t k = 0; k < 2; k++) {
        PkpObject o;
        PocketSummary s;
        const uint8_t* marks = app->marks + (k ? app->cand_count[0] : 0);
        pocket_summarize(marks, app->cand_count[k], &s);
        bool have = pkp_object(app->pack, app->cand_obj[k], &o) == PKP_OK;
        page_add(app->page, PageHeading, have ? str(app, o.title) : "(unreadable)", 0);
        snprintf(line, sizeof(line), "%u/%u markers observed", s.counts[POCKET_OBSERVED], s.total);
        page_add(app->page, PageParagraph, line, 0);
        snprintf(
            line,
            sizeof(line),
            "%u seen,%u no,%u ?,%u n/a",
            s.counts[POCKET_OBSERVED],
            s.counts[POCKET_NOT_OBSERVED],
            s.counts[POCKET_UNKNOWN],
            s.counts[POCKET_NOT_APPLICABLE]);
        page_add(app->page, PageBar, line, 0);
        page_add(app->page, PageLink, "Open the article", T_OBJECT | app->cand_obj[k]);
    }
    page_add(
        app->page,
        PageNote,
        "Two sets of your marks, side by side. Neither count is a score or a verdict.",
        0);
    restore(app);
}

static void render_detect_result(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    if(app->cands == 2) {
        render_compare_result(app);
        return;
    }
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

/* "3 of 4": the session's right answers of all given, once there is one. */
static void quiz_score(UiApp* app) {
    char pos[16]; /* two 16-bit counts: "65535 of 65535" fits */
    if(app->quiz_asked == 0) return;
    snprintf(pos, sizeof(pos), "%u of %u", app->quiz_right, app->quiz_asked);
    page_set_position(app->page, pos);
}

static void save_progress(UiApp* app) {
    size_t n = progress_encode(&app->progress, app->file, PROGRESS_MAX_FILE);
    app->progress_ok = app->platform.progress_write(app->platform.ctx, app->file, n);
}

/* Count the answer for the session and file it in its Leitner box. */
static void score(UiApp* app, uint8_t pick) {
    bool right = pocket_quiz_correct(&app->quiz, pick);
    app->quiz_asked++;
    app->quiz_right += right;
    uint32_t key = progress_key(str(app, app->quiz.prompt));
    progress_record(&app->progress, key, (uint16_t)top(app)->aux, right);
    save_progress(app);
}

static void render_quiz(UiApp* app) {
    page_reset(app->page, "Quiz");
    if(top(app)->filter && top(app)->aux == UINT32_MAX) {
        page_reset(app->page, "Review");
        page_set_title_icon(app->page, UiIconCorrect);
        page_add(app->page, PageParagraph, "Nothing is due now.", 0);
        page_add(
            app->page,
            PageNote,
            "Answered questions come back after 1, 2, 4, 8 or 16 sessions, by their box.",
            0);
        restore(app);
        return;
    }
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
    quiz_score(app);
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
    quiz_score(app);
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
    page_add(
        app->page,
        PageLink,
        f->filter ? "Next due question" : "Next question",
        f->filter ? T_HOME | HomeReview : T_NEXT_QUIZ | app->quiz.kind);
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
    page_add(app->page, PageHeading, "Your progress", 0);
    snprintf(
        line,
        sizeof(line),
        "Answered: %u. Due now: %u. Session %u.",
        app->progress.count,
        progress_due_count(&app->progress),
        app->progress.session);
    page_add(app->page, PageParagraph, line, 0);
    if(!app->progress_ok) page_add(app->page, PageNote, "The last save failed.", 0);
    if(app->progress.count)
        page_add(app->page, PageLink, "Forget my progress", T_HOME | HomeForget);
    restore(app);
}

static void render_forget(UiApp* app) {
    page_reset(app->page, "Forget progress?");
    page_set_title_icon(app->page, UiIconAlert);
    page_add(
        app->page,
        PageParagraph,
        "Deletes every answer and box on the SD card. The pack is not touched.",
        0);
    page_add(app->page, PageLink, "Forget it", T_HOME | HomeForgetNow);
    page_add(app->page, PageMeta, "Back keeps it.", 0);
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
    case ScrHub:
        render_hub(app);
        break;
    case ScrKinds:
        render_kinds(app);
        break;
    case ScrForget:
        render_forget(app);
        break;
    case ScrCompare:
        render_compare(app);
        break;
    default:
        render_about(app);
        break;
    }
}

/* ------------------------------------------------------------------ actions */

/* Append an object's markers (its leading indicator items) to the detector's. */
static void add_candidate(UiApp* app, uint16_t obj) {
    PkpObject o;
    uint8_t k = app->cands++;
    app->cand_obj[k] = obj;
    app->cand_count[k] = 0;
    app->cand_first[k] = UINT32_MAX;
    if(pkp_object(app->pack, obj, &o) != PKP_OK) o.items_count = 0;
    for(uint32_t i = 0; i < o.items_count && app->marker_count < MAX_MARKERS; i++) {
        PkpItem it;
        if(pkp_item(app->pack, o.items_first + i, &it) != PKP_OK) break;
        if(it.type != PKP_ITEM_INDICATOR) break; /* the encoder writes indicators first */
        if(app->cand_count[k] == 0) app->cand_first[k] = o.items_first + i;
        app->cand_count[k]++;
        app->marks[app->marker_count++] = POCKET_UNANSWERED;
    }
}

static void start_detect(UiApp* app, uint16_t obj, int16_t other) {
    app->marker_count = 0;
    app->marker_at = 0;
    app->cands = 0;
    add_candidate(app, obj);
    if(other >= 0) add_candidate(app, (uint16_t)other);
    push(app, (Frame){.screen = ScrDetect, .obj = obj});
}

/* q < 0 (no readable question) opens the quiz screen anyway, which says it could not read one. */
static void start_quiz(UiApp* app, int32_t q, uint16_t obj) {
    push(app, (Frame){.screen = ScrQuiz, .aux = q < 0 ? UINT32_MAX : (uint32_t)q, .obj = obj});
}

/* An object index from a random start, the first at or after it with capability `cap`. */
static uint16_t random_object(UiApp* app, uint8_t cap) {
    uint32_t n = pkp_count(app->pack, PKP_SEC_OBJECTS);
    uint32_t start = app->platform.random(app->platform.ctx) % n;
    for(uint32_t k = 0; k < n; k++) {
        PkpObject o;
        uint32_t i = (start + k) % n;
        if(pkp_object(app->pack, i, &o) == PKP_OK && (o.caps & cap)) return (uint16_t)i;
    }
    return (uint16_t)start; /* nothing readable: the article screen says so */
}

/* The next due question, checked against the pack: an entry whose question is gone or changed
 * (its prompt no longer hashes to its key) is dropped. With none due, the review says so. */
static void start_review(UiApp* app) {
    Frame* f = top(app);
    if(f->screen == ScrQuizResult) app->depth--; /* "Next due question" replaces the answer */
    for(int i; (i = progress_next_due(&app->progress)) >= 0;) {
        ProgressEntry* e = &app->progress.entries[i];
        PkpQuiz q;
        if(pkp_quiz(app->pack, e->quiz, &q) == PKP_OK &&
           progress_key(str(app, q.prompt)) == e->key) {
            push(app, (Frame){.screen = ScrQuiz, .aux = e->quiz, .filter = 1});
            return;
        }
        progress_drop(&app->progress, i);
    }
    push(app, (Frame){.screen = ScrQuiz, .aux = UINT32_MAX, .filter = 1});
}

static void home(UiApp* app, uint32_t entry) {
    static const uint8_t filters[] = {
        [HomeDetect] = PKP_CAP_DETECT,
        [HomeApply] = PKP_CAP_APPLY,
        [HomeCompare] = PKP_CAP_COMPARE,
        [HomeAll] = PKP_CAP_BROWSE,
        [HomeQuizzable] = PKP_CAP_QUIZ,
    };
    switch(entry) {
    case HomeTop:
        app->depth = 1;
        top(app)->sel = 0;
        top(app)->top = 0;
        render(app);
        break;
    case HomeBrowse:
        push(app, (Frame){.screen = ScrCategories});
        break;
    case HomeKinds:
        push(app, (Frame){.screen = ScrKinds});
        break;
    case HomeDetect:
    case HomeApply:
    case HomeCompare:
    case HomeAll:
    case HomeQuizzable:
        push(app, (Frame){.screen = ScrFilter, .filter = filters[entry]});
        break;
    case HomeInfo:
        push(app, (Frame){.screen = ScrInfo});
        break;
    case HomeAbout:
        push(app, (Frame){.screen = ScrAbout});
        break;
    case HomeRandom:
        push(app, (Frame){.screen = ScrArticle, .obj = random_object(app, PKP_CAP_BROWSE)});
        break;
    case HomeRandomDetect:
        start_detect(app, random_object(app, PKP_CAP_DETECT), -1);
        break;
    case HomeReview:
        start_review(app);
        break;
    case HomeForget:
        push(app, (Frame){.screen = ScrForget});
        break;
    case HomeForgetNow:
        app->platform.progress_forget(app->platform.ctx);
        progress_init(&app->progress);
        app->progress_ok = true;
        app->depth--; /* back to Info, which now shows the empty progress */
        render(app);
        break;
    case HomeQuizTerm:
    case HomeQuizExample:
        start_quiz(
            app,
            quiz_random(app, entry == HomeQuizTerm ? PKP_QUIZ_DEFINITION : PKP_QUIZ_TRAINER),
            0);
        break;
    default:
        push(app, (Frame){.screen = ScrHub, .aux = entry - HomeHub});
        break;
    }
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
        start_detect(app, f->obj, -1);
        break;
    case T_PICK:
        push(app, (Frame){.screen = ScrCompare, .obj = (uint16_t)T_ARG(target)});
        break;
    case T_COMPARE:
        start_detect(app, f->obj, (int16_t)T_ARG(target));
        break;
    case T_QUIZ:
        start_quiz(app, quiz_for(app, f->obj), f->obj);
        break;
    case T_CATEGORY:
        push(app, (Frame){.screen = ScrObjects, .cat = (uint16_t)T_ARG(target)});
        break;
    case T_OF_KIND:
        push(app, (Frame){.screen = ScrFilter, .filter = 0, .cat = (uint16_t)T_ARG(target)});
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
        score(app, top(app)->pick);
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
            score(app, f->pick);
            render(app);
        } else {
            push(
                app,
                (Frame){
                    .screen = ScrQuizOptions, .aux = f->aux, .obj = f->obj, .filter = f->filter});
        }
    }
}

/* ------------------------------------------------------------------ lifecycle */

UiApp* ui_app_alloc(const UiPlatform* platform) {
    UiApp* app = calloc(1, sizeof(UiApp));
    app->platform = *platform;
    app->page = page_alloc();
    app->scratch = malloc(SCRATCH);
    app->file = malloc(PROGRESS_MAX_FILE);
    app->progress_ok = true;
    app->pack_status = PKP_E_MISSING;
    app->stack[0] = (Frame){.screen = ScrHome};
    app->depth = 1;
    return app;
}

void ui_app_free(UiApp* app) {
    page_free(app->page);
    free(app->scratch);
    free(app->file);
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
            if(o.kind < KIND_CAP) app->kind_count[o.kind]++;
        }
        for(uint32_t i = 0; i < pkp_count(pack, PKP_SEC_QUIZ); i++) {
            PkpQuiz q;
            if(pkp_quiz(pack, i, &q) == PKP_OK && q.kind < 3) app->quiz_kind_count[q.kind]++;
        }
        app->scan_ms = app->platform.tick_ms(app->platform.ctx) - t0;
    }
    /* the quiz progress: the better of the file and its temporary copy; one more session */
    static uint8_t copy[PROGRESS_MAX_FILE];
    size_t a = app->platform.progress_read(app->platform.ctx, 0, app->file, PROGRESS_MAX_FILE);
    size_t b = app->platform.progress_read(app->platform.ctx, 1, copy, sizeof(copy));
    progress_load(&app->progress, app->file, a, copy, b);
    app->progress.session++;
    if(app->progress.count) save_progress(app); /* the session counts even with no answer */
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
    case PageActHome:
        if(app->depth > 1) home(app, HomeTop);
        break;
    default:
        break;
    }
    return true;
}

const char* ui_app_screen_name(const UiApp* app) {
    static const char* const names[] = {
#define SCREEN_NAME(name) #name,
        SCREENS(SCREEN_NAME)
#undef SCREEN_NAME
    };
    return names[app->stack[app->depth - 1].screen];
}

bool ui_app_tick(UiApp* app) {
    return page_tick(app->page);
}

Page* ui_app_page(UiApp* app) {
    return app->page;
}
