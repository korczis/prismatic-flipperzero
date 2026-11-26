#include "ui_app.h"

#include "../core/pocket.h"
#include "../core/bayes.h"
#include "../core/scorecard.h"
#include "../core/search.h"
#include "../core/simulate.h"
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
#define T_OBJECT       (1u << 24)
#define T_CHAPTER      (2u << 24) /* item offset inside the current object */
#define T_DETECT       (3u << 24)
#define T_QUIZ         (4u << 24)
#define T_NEXT_QUIZ    (5u << 24) /* argument: quiz kind */
#define T_CATEGORY     (6u << 24)
#define T_HOME         (7u << 24) /* argument: a home entry */
#define T_PAGE         (8u << 24) /* argument: 0 previous, 1 next */
#define T_OPTION       (9u << 24) /* argument: quiz option */
#define T_MAP          (10u << 24) /* argument: object at the centre of a map */
#define T_OF_KIND      (11u << 24) /* argument: object kind */
#define T_PICK         (12u << 24) /* argument: object whose look-alikes to list */
#define T_COMPARE      (13u << 24) /* argument: the look-alike to compare the frame's object with */
#define T_ASSESS       (14u << 24) /* start the frame object's assessment */
#define T_SAVE         (16u << 24) /* save the detector's marks as a session */
#define SESSIONS_SHOWN 40u
#define T_SIMULATE     (17u << 24) /* start (or restart) the frame object's simulation */
#define T_SIM_PICK     (18u << 24) /* argument: the option chosen in the current step */
#define T_SIM_NEXT     (19u << 24) /* the next step, or the outcome after the last */
#define T_SCORE        (20u << 24) /* start (or restart) the frame object's scorecard */
#define T_SCORE_Q      (21u << 24) /* argument: a question whose answer Left/Right/OK change */
#define T_SCORE_DONE   (24u << 24) /* the scorecard's result */
#define T_AXIS         (25u << 24) /* argument: a scale whose grade Left/Right/OK change */
#define T_GRADE_DONE   (26u << 24) /* the assessment's grade */
#define T_CALC         (22u << 24) /* argument: a calculator action (CALC_*) */
#define T_CALC_OPEN    (23u << 24) /* open the frame object's calculator */
#define MAX_QUESTIONS  12u
#define MAX_BANDS      8u
#define MAX_STEPS      12u
#define MAX_OPTIONS    8u
#define MAX_AXES       4u
#define MAX_LEVELS     16u
#define KIND_CAP       32u
#define T_KIND(t)      ((t) & 0xFF000000u)
#define T_ARG(t)       ((t) & 0x00FFFFFFu)

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
    X(Compare)      \
    X(Assess)       \
    X(AssessResult) \
    X(Sessions)     \
    X(SimStep)      \
    X(SimResult)    \
    X(SimOutcome)   \
    X(Score)        \
    X(ScoreResult)  \
    X(Calc)         \
    X(Find)         \
    X(FindResults)

typedef enum {
#define SCREEN_ENUM(name) Scr##name,
    SCREENS(SCREEN_ENUM)
#undef SCREEN_ENUM
} Screen;

enum {
    CALC_PRIOR, /* a stepper: Left/Right move the prior */
    CALC_UNDO,
    CALC_RESET,
    CALC_ADD, /* a stepper: Left/Right choose a confidence, OK adds a source at it */
};

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
    HomeAssess,
    HomeSimulate,
    HomeScore,
    HomeCalc,
    HomeForget,
    HomeForgetNow,
    HomeSessions,
    HomeForgetSessions,
    HomeForgetSessionsNow,
    HomeFind,
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
    uint16_t cap_count[8];
    uint32_t quiz_kind_count[3];
    uint16_t kind_count[KIND_CAP];
    uint8_t calc_kind; /* the kind "algorithm", which calculators are */

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
    /* an assessment: its scales (the item of each scale's name, how many grades follow) and
     * the grade chosen on each */
    uint8_t axes;
    uint32_t axis_item[MAX_AXES];
    uint8_t axis_levels[MAX_AXES];
    uint8_t grade[MAX_AXES];
    /* a simulation: its starting trust, each step's prompt item and option count, the picks */
    int16_t sim_trust0;
    uint8_t sim_steps;
    uint32_t sim_step_item[MAX_STEPS];
    uint8_t sim_step_options[MAX_STEPS];
    uint8_t sim_pick[MAX_STEPS];
    /* a scorecard: base and clamp, each question's item and choice count, the picks, bands */
    int16_t sc_base, sc_min, sc_max;
    uint8_t sc_questions;
    uint32_t sc_q_item[MAX_QUESTIONS];
    uint8_t sc_q_choices[MAX_QUESTIONS];
    uint8_t sc_pick[MAX_QUESTIONS];
    uint32_t sc_band_item;
    uint8_t sc_bands;
    char sc_band_text[160];
    /* a calculator: its spec, the prior and the sources added, in percent */
    CalcSpec calc;
    uint8_t calc_prior;
    uint8_t calc_src[CALC_MAX_SOURCES];
    uint8_t calc_n;
    uint8_t calc_pick; /* the confidence the add stepper shows, an index into calc.choices */

    PkpQuiz quiz;
    uint16_t quiz_asked; /* this session's answers, shown in the quiz header */
    uint16_t quiz_right;
    Progress progress; /* Leitner boxes, loaded from and saved to storage */
    uint8_t* file; /* PROGRESS_MAX_FILE bytes for reading and writing it */
    bool progress_ok; /* the last save succeeded */

    /* Find by name: the engine, opened (and its index verified) once per pack, the wheel's
     * letter, and how the last letter went */
    PkpSearch search;
    bool search_open;
    PkpStatus search_status; /* of pkp_search_init: PKP_OK, or why there is no search */
    uint32_t search_ms; /* how long opening it took, the index check included */
    uint8_t wheel; /* a position in PKP_SEARCH_LETTERS */
    PkpStatus search_error; /* of the last push or pop */
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
static PkpStatus search_ready(UiApp* app);

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
        [HomeAssess] = app->cap_count[cap_index(PKP_CAP_ASSESS)],
        [HomeSimulate] = app->cap_count[cap_index(PKP_CAP_SIMULATE)],
        [HomeScore] = app->cap_count[cap_index(PKP_CAP_SCORE)],
        [HomeCalc] = app->kind_count[app->calc_kind],
        [HomeFind] = total, /* always there: the screen says when the pack has no index */
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
     {{UiIconFind, HomeFind, "Find by name", "", "Type it a letter at a time", ShareNone},
      {UiIconFolder, HomeBrowse, "By chapter", "%lu", "Chapters in book order", ShareNone},
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
      {UiIconCompare, HomeCompare, "Confusables", "%lu", "Pairs to tell apart", ShareObjects},
      {UiIconCorrect, HomeAssess, "Assess", "%lu", "Grade a source, a claim, a finding", ShareNone},
      {UiIconCorrect,
       HomeScore,
       "Score a case",
       "%lu",
       "Answer, see the score and its band",
       ShareNone},
      {UiIconCompare,
       HomeCalc,
       "Calculators",
       "%lu",
       "Change the inputs, watch the result",
       ShareNone}}},
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
      {UiIconTrainer,
       HomeSimulate,
       "Crisis simulations",
       "%lu",
       "Decide, see what follows",
       ShareNone},
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
        "Everything",
        "Detectors",
        "What to do",
        "Quiz",
        "Confusables",
        "Assess",
        "Simulations",
        "Scorecards"};
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

/* A grade's text "code|label|meaning", split in place. */
static void split_level(char* text, const char** code, const char** label, const char** meaning) {
    *code = text;
    *label = *meaning = "";
    char* tab = strchr(text, '|');
    if(!tab) return;
    *tab = '\0';
    *label = tab + 1;
    tab = strchr(tab + 1, '|');
    if(!tab) return;
    *tab = '\0';
    *meaning = tab + 1;
}

/* An assessment's scales in an article: each scale a heading, its grades bullets. */
static void add_scales(UiApp* app, const PkpObject* o) {
    char line[160];
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o->items_first + k, &it) != PKP_OK) continue;
        if(it.type == PKP_ITEM_AXIS)
            page_add_icon(app->page, PageHeading, UiIconCorrect, str(app, it.text), 0);
        if(it.type != PKP_ITEM_LEVEL) continue;
        const char *code, *label, *meaning;
        split_level((char*)str(app, it.text), &code, &label, &meaning);
        snprintf(line, sizeof(line), "%s  %s%s%s", code, label, *meaning ? ": " : "", meaning);
        page_add(app->page, PageBullet, line, 0);
    }
}

/* Pick the look-alike to run side by side with the object. */
static void render_compare(UiApp* app) {
    PkpObject o;
    page_reset(app->page, "Compare with");
    page_set_title_icon(app->page, UiIconCompare);
    if(pkp_object(app->pack, top(app)->obj, &o) == PKP_OK) look_alikes(app, &o, app->page);
    restore(app);
}

/* An assessment on one screen: each scale a stepper whose grade Left/Right change at once; the
 * code builds in the header as you go, the focused grade's meaning in the status line. */
static void render_assess(UiApp* app) {
    char code[24], value[96], name[64];
    PkpItem it;
    page_reset(app->page, "Assess");
    page_set_title_icon(app->page, UiIconCorrect);
    if(app->axes == 0) {
        page_add(app->page, PageNote, "This assessment could not be read.", 0);
        restore(app);
        return;
    }
    size_t at = 0;
    for(uint8_t a = 0; a < app->axes; a++) {
        const char *lcode, *label, *meaning;
        snprintf(
            name,
            sizeof(name),
            "%s",
            pkp_item(app->pack, app->axis_item[a], &it) == PKP_OK ? str(app, it.text) : "?");
        if(pkp_item(app->pack, app->axis_item[a] + 1 + app->grade[a], &it) != PKP_OK) continue;
        split_level((char*)str(app, it.text), &lcode, &label, &meaning);
        at += (size_t)snprintf(code + at, sizeof(code) - at, "%s", lcode);
        if(at >= sizeof(code)) at = sizeof(code) - 1;
        snprintf(value, sizeof(value), "%.10s %.60s", lcode, label);
        page_add_stepper(app->page, 0, name, value, T_AXIS | a);
        page_set_hint(app->page, *meaning ? meaning : "Left/Right change the grade");
    }
    page_set_position(app->page, code);
    page_add_row(app->page, UiIconCorrect, "See the grade", NULL, T_GRADE_DONE);
    page_set_hint(app->page, "Left/Right change a grade");
    restore(app);
}

/* The grade: the chosen codes together, then each scale's grade and what it means, then the
 * object's guidance and grading rules. */
static void render_assess_result(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    char big[48], line[160];
    page_reset(app->page, "Your grade");
    page_set_title_icon(app->page, UiIconCorrect);
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK) {
        page_add(app->page, PageNote, "This assessment could not be read.", 0);
        return;
    }
    size_t at = 0;
    for(uint8_t a = 0; a < app->axes; a++) {
        PkpItem it;
        if(pkp_item(app->pack, app->axis_item[a] + 1 + app->grade[a], &it) != PKP_OK) continue;
        const char *code, *label, *meaning;
        split_level((char*)str(app, it.text), &code, &label, &meaning);
        at += (size_t)snprintf(big + at, sizeof(big) - at, "%s", code);
        if(at >= sizeof(big)) at = sizeof(big) - 1;
    }
    snprintf(big + at, sizeof(big) - at, " %s", o.code.len ? str(app, o.code) : "");
    page_add(app->page, PageBig, big, 0);
    for(uint8_t a = 0; a < app->axes; a++) {
        PkpItem it;
        if(pkp_item(app->pack, app->axis_item[a], &it) != PKP_OK) continue;
        page_add(app->page, PageHeading, str(app, it.text), 0);
        if(pkp_item(app->pack, app->axis_item[a] + 1 + app->grade[a], &it) != PKP_OK) continue;
        const char *code, *label, *meaning;
        split_level((char*)str(app, it.text), &code, &label, &meaning);
        snprintf(line, sizeof(line), "%s  %s", code, label);
        page_add(app->page, PageParagraph, line, 0);
        if(*meaning) page_add(app->page, PageNote, meaning, 0);
    }
    section(app, &o, PKP_ITEM_ACTION, "What to do with it");
    section(app, &o, PKP_ITEM_VALIDATION, "How to grade");
    page_add(app->page, PageLink, "Assess again", T_ASSESS);
    page_add(app->page, PageLink, str(app, o.title), T_OBJECT | f->obj);
    restore(app);
}

/* ------------------------------------------------------------------ simulations */

static const char sim_disclaimer[] =
    "A training exercise from the Prismatic Platform, not guidance for a real emergency. "
    "In an emergency, call 112.";

/* One option of a step, split into its fields; false when unreadable. */
static bool sim_option(UiApp* app, uint8_t step, uint8_t k, SimOption* op) {
    PkpItem it;
    return pkp_item(app->pack, app->sim_step_item[step] + 1 + k, &it) == PKP_OK &&
           sim_option_split((char*)str(app, it.text), op);
}

/* The run's state after the picks of steps 0..last, recomputed: Back and a new pick never
 * count a decision twice. */
static SimState sim_state(UiApp* app, int last) {
    SimState st;
    sim_start(&st, app->sim_trust0);
    for(int k = 0; k <= last; k++) {
        SimOption op;
        if(sim_option(app, (uint8_t)k, app->sim_pick[k], &op)) sim_apply(&st, op.impact, op.delta);
    }
    return st;
}

static void render_sim_step(UiApp* app) {
    Frame* f = top(app);
    char line[200], pos[24];
    PkpItem it;
    if(app->sim_steps == 0 || pkp_item(app->pack, app->sim_step_item[f->aux], &it) != PKP_OK) {
        page_reset(app->page, "Simulation");
        page_add(app->page, PageNote, "This simulation could not be read.", 0);
        restore(app);
        return;
    }
    SimState st = sim_state(app, (int)f->aux - 1);
    snprintf(line, sizeof(line), "Step %lu of %u", (unsigned long)f->aux + 1, app->sim_steps);
    page_reset(app->page, line);
    page_set_title_icon(app->page, UiIconTrainer);
    snprintf(pos, sizeof(pos), "trust %d", st.trust);
    page_set_position(app->page, pos);
    page_add(app->page, PageParagraph, str(app, it.text), 0);
    for(uint8_t k = 0; k < app->sim_step_options[f->aux]; k++) {
        SimOption op;
        if(!sim_option(app, (uint8_t)f->aux, k, &op)) continue;
        snprintf(line, sizeof(line), "%.150s (%s risk)", op.option, op.risk);
        page_add(app->page, PageLink, line, T_SIM_PICK | k);
    }
    if(f->aux == 0) page_add(app->page, PageNote, sim_disclaimer, 0);
    restore(app);
}

/* What the pick led to: the consequence, and trust before and after. */
static void render_sim_result(UiApp* app) {
    Frame* f = top(app);
    char line[64];
    SimOption op;
    page_reset(app->page, "Consequence");
    if(!sim_option(app, (uint8_t)f->aux, app->sim_pick[f->aux], &op)) {
        page_add(app->page, PageNote, "This step could not be read.", 0);
        restore(app);
        return;
    }
    char type[16];
    snprintf(type, sizeof(type), "%s", op.type);
    type[0] = (char)(type[0] >= 'a' && type[0] <= 'z' ? type[0] - 32 : type[0]);
    page_reset(app->page, type);
    page_set_title_icon(app->page, op.delta >= 0 ? UiIconCorrect : UiIconAlert);
    page_add(app->page, PageMeta, op.option, 0);
    page_add(app->page, PageParagraph, op.consequence, 0);
    SimState before = sim_state(app, (int)f->aux - 1), after = sim_state(app, (int)f->aux);
    snprintf(line, sizeof(line), "%d trust (%+d)", after.trust, after.trust - before.trust);
    page_add(app->page, PageBig, line, 0);
    snprintf(line, sizeof(line), "%d trust,%d to go", after.trust, 100 - after.trust);
    page_add(app->page, PageBar, line, 0);
    bool last = f->aux + 1 >= app->sim_steps;
    page_add(app->page, PageLink, last ? "See the outcome" : "Next step", T_SIM_NEXT);
    restore(app);
}

/* The outcome by the source's rules, then every decision with its trust change. */
static void render_sim_outcome(UiApp* app) {
    char line[200];
    SimState st = sim_state(app, app->sim_steps - 1);
    page_reset(app->page, "Outcome");
    page_set_title_icon(app->page, UiIconTrainer);
    page_add(app->page, PageTitle, sim_outcome_name(sim_outcome(&st)), 0);
    snprintf(
        line,
        sizeof(line),
        "Trust %d. %u positive and %u negative decisions of %u.",
        st.trust,
        st.positive,
        st.negative,
        st.decisions);
    page_add(app->page, PageParagraph, line, 0);
    page_add(app->page, PageHeading, "Your decisions", 0);
    for(uint8_t k = 0; k < app->sim_steps; k++) {
        SimOption op;
        if(!sim_option(app, k, app->sim_pick[k], &op)) continue;
        snprintf(line, sizeof(line), "%u. %+d %.150s", k + 1, op.delta, op.option);
        page_add(app->page, PageBullet, line, 0);
    }
    page_add(app->page, PageLink, "Run it again", T_SIMULATE);
    page_add(
        app->page,
        PageNote,
        "Optimal: trust 70+ and more positive than negative decisions. Acceptable: trust 40+ and "
        "no more negative than positive. Critical failure: trust under 20.",
        0);
    page_add(app->page, PageNote, sim_disclaimer, 0);
    restore(app);
}

/* ------------------------------------------------------------------ scorecards */

/* The score of the choices made on questions 0..last, clamped. */
static int score_total(UiApp* app, int last) {
    int sum = app->sc_base;
    for(int q = 0; q <= last; q++) {
        PkpItem it;
        ScoreChoice ch;
        if(pkp_item(app->pack, app->sc_q_item[q] + 1 + app->sc_pick[q], &it) == PKP_OK &&
           score_choice((char*)str(app, it.text), &ch))
            sum += ch.weight;
    }
    return score_clamp(sum, app->sc_min, app->sc_max);
}

/* The band of a score: the first whose top it does not exceed, its texts kept in the app (the
 * string reader reuses its buffer). */
static bool score_band_of(UiApp* app, int score, ScoreBand* band) {
    for(uint8_t b = 0; b < app->sc_bands; b++) {
        PkpItem it;
        if(pkp_item(app->pack, app->sc_band_item + b, &it) != PKP_OK) continue;
        snprintf(app->sc_band_text, sizeof(app->sc_band_text), "%s", str(app, it.text));
        if(score_band(app->sc_band_text, band) && score <= band->top) return true;
    }
    return false;
}

/* A scorecard on one screen: each question a stepper whose answer Left/Right change at once;
 * the score and its band follow in the header, the focused answer's note (or the question) in
 * the status line. */
static void render_score(UiApp* app) {
    char pos[24], value[64];
    page_reset(app->page, "Score");
    page_set_title_icon(app->page, UiIconCorrect);
    if(app->sc_questions == 0) {
        page_add(app->page, PageNote, "This scorecard could not be read.", 0);
        restore(app);
        return;
    }
    int score = score_total(app, app->sc_questions - 1);
    ScoreBand band;
    snprintf(pos, sizeof(pos), "%d %s", score, score_band_of(app, score, &band) ? band.code : "");
    page_set_position(app->page, pos);
    for(uint8_t q = 0; q < app->sc_questions; q++) {
        PkpItem it;
        ScoreChoice ch;
        char question[160];
        bool read = pkp_item(app->pack, app->sc_q_item[q], &it) == PKP_OK;
        if(read) snprintf(question, sizeof(question), "%s", str(app, it.text));
        read = read &&
               pkp_item(app->pack, app->sc_q_item[q] + 1 + app->sc_pick[q], &it) == PKP_OK &&
               score_choice((char*)str(app, it.text), &ch);
        if(!read) continue; /* the card went away: the row is left out */
        snprintf(
            value,
            sizeof(value),
            "%+d %.30s",
            ch.weight,
            ch.label); /* the weight first: it survives shortening */
        page_add_stepper(app->page, 0, question, value, T_SCORE_Q | q);
        page_set_hint(app->page, *ch.note ? ch.note : question);
    }
    page_add_row(app->page, UiIconCorrect, "See the result", NULL, T_SCORE_DONE);
    page_set_hint(app->page, "Left/Right change an answer");
    restore(app);
}

/* The score, its band, what moved it (with the notes of the choices made), then the guidance. */
static void render_score_result(UiApp* app) {
    Frame* f = top(app);
    PkpObject o;
    char line[200];
    page_reset(app->page, "Your score");
    page_set_title_icon(app->page, UiIconCorrect);
    if(pkp_object(app->pack, f->obj, &o) != PKP_OK) {
        page_add(app->page, PageNote, "This scorecard could not be read.", 0);
        restore(app);
        return;
    }
    int score = score_total(app, app->sc_questions - 1);
    ScoreBand band = {0, "?", "", ""};
    score_band_of(app, score, &band);
    snprintf(line, sizeof(line), "%d %s %s", score, band.code, band.label);
    page_add(app->page, PageBig, line, 0);
    snprintf(line, sizeof(line), "%d score,%d to max", score - app->sc_min, app->sc_max - score);
    page_add(app->page, PageBar, line, 0);
    if(*band.meaning) page_add(app->page, PageNote, band.meaning, 0);
    page_add(app->page, PageHeading, "What moved it", 0);
    for(uint8_t q = 0; q < app->sc_questions; q++) {
        PkpItem it;
        ScoreChoice ch;
        if(pkp_item(app->pack, app->sc_q_item[q] + 1 + app->sc_pick[q], &it) != PKP_OK ||
           !score_choice((char*)str(app, it.text), &ch) || ch.weight == 0)
            continue;
        snprintf(line, sizeof(line), "%+d %.150s", ch.weight, ch.label);
        page_add(app->page, PageBullet, line, 0);
        if(*ch.note) page_add(app->page, PageNote, ch.note, 0);
    }
    page_add(app->page, PageLink, "Score again", T_SCORE);
    section(app, &o, PKP_ITEM_ACTION, "What to do with it");
    section(app, &o, PKP_ITEM_VALIDATION, "How it is scored");
    restore(app);
}

/* ------------------------------------------------------------------ calculators */

/* The object's calculator item, parsed; false when it has none or it is unreadable. */
static bool calc_of(UiApp* app, const PkpObject* o, CalcSpec* spec) {
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o->items_first + k, &it) == PKP_OK && it.type == PKP_ITEM_CALC)
            return calc_parse(str(app, it.text), spec);
    }
    return false;
}

/* A Bayes calculator: the belief and its status on top, then the actions as rows. */
static void render_calc(UiApp* app) {
    char line[96];
    page_reset(app->page, "Bayes update");
    page_set_title_icon(app->page, UiIconCompare);
    if(strcmp(app->calc.algorithm, "bayes") != 0) {
        page_add(app->page, PageNote, "This calculator could not be read.", 0);
        restore(app);
        return;
    }
    float p = 100.0f * bayes_fuse((float)app->calc_prior / 100.0f, app->calc_src, app->calc_n);
    const char* status = bayes_status(p, app->calc.confirm, app->calc.reject);
    /* the result stays in view: the belief in the header, the state in the status line */
    char pos[12], hint[96];
    snprintf(pos, sizeof(pos), "%.1f%%", (double)p);
    page_set_position(app->page, pos);
    int at = snprintf(hint, sizeof(hint), "%s; from %u%%:", status, app->calc_prior);
    for(uint8_t k = 0; k < app->calc_n && at < (int)sizeof(hint) - 5; k++)
        at += snprintf(hint + at, sizeof(hint) - (size_t)at, " %u", app->calc_src[k]);
    snprintf(line, sizeof(line), "%u%%", app->calc.choices[app->calc_pick]);
    page_add_stepper(app->page, UiIconDetect, "Add a source", line, T_CALC | CALC_ADD);
    page_set_hint(app->page, hint);
    char prior[8];
    snprintf(prior, sizeof(prior), "%u%%", app->calc_prior);
    page_add_stepper(app->page, 0, "Prior", prior, T_CALC | CALC_PRIOR);
    page_set_hint(app->page, hint);
    page_add_row(app->page, 0, "Remove the last source", NULL, T_CALC | CALC_UNDO);
    page_set_hint(app->page, hint);
    page_add_row(app->page, 0, "Start over", NULL, T_CALC | CALC_RESET);
    page_set_hint(app->page, hint);
    /* the controls come first, so they are on the first screen; the picture of the belief
     * follows for a closer look (the header and status line already carry it) */
    snprintf(line, sizeof(line), "%.1f %% %s", (double)p, status);
    page_add(app->page, PageBig, line, 0);
    int belief = (int)(p + 0.5f);
    snprintf(line, sizeof(line), "%d belief,%d doubt", belief, 100 - belief);
    page_add(app->page, PageBar, line, 0);
    restore(app);
}

static void start_calc(UiApp* app, uint16_t obj) {
    PkpObject o;
    memset(&app->calc, 0, sizeof(app->calc));
    if(pkp_object(app->pack, obj, &o) != PKP_OK || !calc_of(app, &o, &app->calc))
        app->calc.algorithm[0] = '\0';
    app->calc_prior = app->calc.prior;
    app->calc_n = 0;
    app->calc_pick = (uint8_t)(app->calc.n_choices / 2); /* the middle of the choices */
    push(app, (Frame){.screen = ScrCalc, .obj = obj});
}

/* The prior in steps of ten, within 10..90 (a belief of 0 or 100 no source could move). */
static void calc_step_prior(UiApp* app, int dir) {
    if(dir < 0 && app->calc_prior > 10) app->calc_prior -= 10;
    if(dir > 0 && app->calc_prior < 90) app->calc_prior += 10;
}

/* One action of the calculator; the focus stays on the row pressed. */
static void calc_action(UiApp* app, uint32_t action) {
    if(action == CALC_ADD && app->calc_n < CALC_MAX_SOURCES)
        app->calc_src[app->calc_n++] = app->calc.choices[app->calc_pick];
    if(action == CALC_PRIOR) calc_step_prior(app, 1); /* OK on the prior steps it up */
    if(action == CALC_UNDO && app->calc_n) app->calc_n--;
    if(action == CALC_RESET) {
        app->calc_n = 0;
        app->calc_prior = app->calc.prior;
    }
    top(app)->sel = T_CALC | action;
    render(app);
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
    if(o.caps & PKP_CAP_ASSESS)
        page_add_icon(app->page, PageChip, UiIconCorrect, "Assess", T_ASSESS);
    if(o.caps & PKP_CAP_SIMULATE)
        page_add_icon(app->page, PageChip, UiIconTrainer, "Run", T_SIMULATE);
    if(o.caps & PKP_CAP_SCORE) page_add_icon(app->page, PageChip, UiIconCorrect, "Score", T_SCORE);
    CalcSpec calc;
    bool has_calc = calc_of(app, &o, &calc);
    if(has_calc) page_add_icon(app->page, PageChip, UiIconCompare, "Calculate", T_CALC_OPEN);
    if(o.caps & PKP_CAP_QUIZ) page_add_icon(app->page, PageChip, UiIconQuiz, "Quiz", T_QUIZ);
    if(o.rel_count) {
        snprintf(line, sizeof(line), "%u", o.rel_count);
        page_add_icon(app->page, PageChip, UiIconMap, line, T_MAP | f->obj);
    }
    page_add(app->page, PageParagraph, str(app, o.definition), 0);

    section(app, &o, PKP_ITEM_INDICATOR, "How to recognise it");
    add_scales(app, &o);
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
    if(o.caps & PKP_CAP_ASSESS) page_add_menu(app->page, "Assess", T_ASSESS);
    if(o.caps & PKP_CAP_SIMULATE) page_add_menu(app->page, "Run the simulation", T_SIMULATE);
    if(o.caps & PKP_CAP_SCORE) page_add_menu(app->page, "Score it", T_SCORE);
    if(has_calc) page_add_menu(app->page, "Calculate", T_CALC_OPEN);
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

/* Saving is the reader's choice: a link until pressed, then what became of it. */
static void save_link(UiApp* app) {
    uint8_t saved = top(app)->pick;
    if(saved == 1)
        page_add(app->page, PageNote, "Saved: Info > Saved sessions.", 0);
    else if(saved == 2)
        page_add(
            app->page, PageNote, "Not saved: the card is missing or the sessions are full.", 0);
    else
        page_add(app->page, PageLink, "Save these marks", T_SAVE);
}

/* True when the sessions file ends inside a line (a torn append). */
static bool session_tail_open(UiApp* app) {
    char chunk[256];
    char last = '\n';
    uint32_t pos = 0;
    for(size_t got;
        (got = app->platform.session_read(app->platform.ctx, pos, chunk, sizeof(chunk)));
        pos += (uint32_t)got)
        last = chunk[got - 1];
    return last != '\n';
}

/* The detector's marks as one session line, appended by the platform. */
static void save_session(UiApp* app) {
    char time[24], pack[64], ids[SESSION_MAX_CANDS][96];
    SessionCand c[SESSION_MAX_CANDS];
    PkpStr s;
    app->platform.now(app->platform.ctx, time, sizeof(time));
    snprintf(
        pack,
        sizeof(pack),
        "%s",
        pkp_meta(app->pack, PKP_META_PACK_ID, &s) == PKP_OK ? str(app, s) : "?");
    uint16_t at = 0;
    for(uint8_t k = 0; k < app->cands; k++) {
        PkpObject o;
        snprintf(
            ids[k],
            sizeof(ids[k]),
            "%s",
            pkp_object(app->pack, app->cand_obj[k], &o) == PKP_OK ? str(app, o.id) : "?");
        c[k] = (SessionCand){ids[k], app->cand_obj[k], app->marks + at, app->cand_count[k]};
        at = (uint16_t)(at + app->cand_count[k]);
    }
    /* a line torn off by a lost write is closed first, so this one starts on its own line */
    char line[SESSION_MAX_LINE + 1];
    size_t lead = session_tail_open(app) ? 1 : 0;
    line[0] = '\n';
    size_t n = session_line(line + lead, sizeof(line) - lead, time, pack, c, app->cands);
    bool ok = n && app->platform.session_append(app->platform.ctx, line, n + lead);
    top(app)->pick = ok ? 1 : 2;
    top(app)->sel = 0;
    render(app);
}

/* Every whole session line in the file, oldest first: `found` gets each one's offset and
 * length (the newest SESSIONS_SHOWN of them, in a ring); returns how many there are. */
static uint32_t scan_sessions(UiApp* app, uint32_t* offs, uint16_t* lens) {
    char chunk[256], line[SESSION_MAX_LINE];
    uint32_t pos = 0, start = 0, total = 0;
    size_t len = 0;
    for(size_t got;
        (got = app->platform.session_read(app->platform.ctx, pos, chunk, sizeof(chunk)));
        pos += (uint32_t)got) {
        for(size_t i = 0; i < got; i++) {
            if(chunk[i] != '\n') {
                if(len < sizeof(line)) line[len] = chunk[i];
                len++;
                continue;
            }
            SessionView v;
            if(len < sizeof(line) && session_parse(line, len, &v)) {
                offs[total % SESSIONS_SHOWN] = start;
                lens[total % SESSIONS_SHOWN] = (uint16_t)len;
                total++;
            }
            start = pos + (uint32_t)i + 1;
            len = 0;
        }
    }
    return total;
}

/* Saved sessions, newest first: when, what, and the marks observed; OK opens the object. */
static void render_sessions(UiApp* app) {
    static uint32_t offs[SESSIONS_SHOWN];
    static uint16_t lens[SESSIONS_SHOWN];
    char line[SESSION_MAX_LINE], label[128], right[16];
    page_reset(app->page, "Saved sessions");
    page_set_title_icon(app->page, UiIconArticle);
    uint32_t total = scan_sessions(app, offs, lens);
    uint32_t shown = total < SESSIONS_SHOWN ? total : SESSIONS_SHOWN;
    for(uint32_t k = 0; k < shown; k++) {
        uint32_t r = (total - 1 - k) % SESSIONS_SHOWN; /* newest first */
        SessionView v;
        size_t n = app->platform.session_read(app->platform.ctx, offs[r], line, lens[r]);
        if(n != lens[r] || !session_parse(line, n, &v)) continue;
        PkpObject o;
        bool here = pkp_object(app->pack, v.index[0], &o) == PKP_OK &&
                    strcmp(str(app, o.id), v.id[0]) == 0;
        /* "11-25 12:00 Title": the date and time without the year and seconds */
        snprintf(
            label,
            sizeof(label),
            "%.5s %.5s %.100s",
            v.time + 5,
            v.time + 11,
            here ? str(app, o.title) : v.id[0]);
        snprintf(
            right, sizeof(right), "%u/%u%s", v.observed[0], v.total[0], v.cands > 1 ? "+" : "");
        if(!here) { /* about an object this pack does not have: shown, not openable */
            page_add(app->page, PageMeta, label, 0);
            continue;
        }
        page_add_row(app->page, UiIconDetect, label, right, T_OBJECT | v.index[0]);
        page_set_hint(app->page, v.time);
    }
    restore(app); /* an empty list (the file gone meanwhile) gets restore's own note */
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
    save_link(app);
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
    save_link(app);
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
        if(pkp_has_search(app->pack)) {
            snprintf(
                line,
                sizeof(line),
                "%lu search keys, %lu postings",
                (unsigned long)pkp_count(app->pack, PKP_SEC_SEARCH_KEYS),
                (unsigned long)pkp_count(app->pack, PKP_SEC_SEARCH_POSTINGS));
            page_add(app->page, PageBullet, line, 0);
        }
        page_add(app->page, PageHeading, "Measured here", 0);
        snprintf(line, sizeof(line), "Open+CRC: %lu ms", (unsigned long)app->open_ms);
        page_add(app->page, PageBullet, line, 0);
        snprintf(line, sizeof(line), "Index scan: %lu ms", (unsigned long)app->scan_ms);
        page_add(app->page, PageBullet, line, 0);
        snprintf(line, sizeof(line), "Pack size: %lu B", (unsigned long)app->pack->size);
        page_add(app->page, PageBullet, line, 0);
        PkpStatus st = pkp_has_search(app->pack) ? search_ready(app) : PKP_E_MISSING;
        if(st == PKP_OK)
            snprintf(line, sizeof(line), "Find index: %lu ms", (unsigned long)app->search_ms);
        else
            snprintf(
                line,
                sizeof(line),
                "Find index: %s",
                st == PKP_E_MISSING ? "none in this pack" : pkp_status_text(st));
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
    static uint32_t offs[SESSIONS_SHOWN];
    static uint16_t lens[SESSIONS_SHOWN];
    uint32_t saved = scan_sessions(app, offs, lens);
    page_add(app->page, PageHeading, "Saved sessions", 0);
    snprintf(line, sizeof(line), "Saved: %lu. Only when you press Save.", (unsigned long)saved);
    page_add(app->page, PageParagraph, line, 0);
    if(saved) {
        page_add(app->page, PageLink, "Open saved sessions", T_HOME | HomeSessions);
        page_add(app->page, PageLink, "Delete saved sessions", T_HOME | HomeForgetSessions);
    }
    restore(app);
}

static void render_forget(UiApp* app) {
    bool sessions = top(app)->filter;
    page_reset(app->page, sessions ? "Delete sessions?" : "Forget progress?");
    page_set_title_icon(app->page, UiIconAlert);
    page_add(
        app->page,
        PageParagraph,
        sessions ? "Deletes every saved session on the SD card. The pack is not touched." :
                   "Deletes every answer and box on the SD card. The pack is not touched.",
        0);
    page_add(
        app->page,
        PageLink,
        sessions ? "Delete them" : "Forget it",
        T_HOME | (sessions ? HomeForgetSessionsNow : HomeForgetNow));
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

/* ------------------------------------------------------------------ find by name */

#define FIND_TOP 3u /* titles Find shows under the wheel */

/* The search engine over this pack, opened once (pkp_search_init verifies the index first, so
 * this is also the check Info shows); a failed read is tried again next time. */
static PkpStatus search_ready(UiApp* app) {
    if(!app->search_open || app->search_status == PKP_E_IO) {
        uint32_t t0 = app->platform.tick_ms(app->platform.ctx);
        app->search_status = pkp_search_init(&app->search, app->pack);
        app->search_ms = app->platform.tick_ms(app->platform.ctx) - t0;
        app->search_open = true;
    }
    return app->search_status;
}

/* The next offered letter from `at` in direction `dir` (wrapping); for 0, `at` itself when
 * offered, else the next one after it. */
static uint8_t wheel_step(uint64_t mask, uint8_t at, int dir) {
    int step = dir ? dir : 1;
    for(uint32_t k = dir ? 1 : 0; k <= PKP_SEARCH_LETTER_COUNT; k++) {
        uint32_t i = (at + PKP_SEARCH_LETTER_COUNT * 2 + (uint32_t)(step * (int)k)) %
                     PKP_SEARCH_LETTER_COUNT;
        if(mask & (1ull << i)) return (uint8_t)i;
    }
    return at;
}

/* Five letters around the current one, none twice: "cd" e "fg", spaces where there are none,
 * and '|' where the wheel wraps from its last letter to its first. */
static void wheel_window(uint64_t mask, uint8_t at, char out[8]) {
    char slot[5];
    memset(slot, ' ', 5);
    uint32_t n = 0;
    for(uint32_t i = 0; i < PKP_SEARCH_LETTER_COUNT; i++)
        n += (mask >> i) & 1u;
    if(n) {
        uint32_t before = (n - 1) / 2 < 2 ? (n - 1) / 2 : 2, after = n - 1 - before;
        if(after > 2) after = 2;
        slot[2] = pkp_search_letter(at);
        for(uint32_t k = 1, i = at; k <= before; k++)
            slot[2 - k] = pkp_search_letter(i = wheel_step(mask, (uint8_t)i, -1));
        for(uint32_t k = 1, i = at; k <= after; k++)
            slot[2 + k] = pkp_search_letter(i = wheel_step(mask, (uint8_t)i, 1));
    }
    size_t m = 0;
    for(int i = 0; i < 5; i++) {
        if(i && slot[i] != ' ' && slot[i - 1] != ' ' &&
           pkp_search_position(slot[i]) < pkp_search_position(slot[i - 1]))
            out[m++] = '|';
        out[m++] = slot[i];
    }
    out[m] = '\0';
}

/* An object's title after its code, when the title does not start with the code already: a
 * match found by its code shows why. */
static const char* coded_title(UiApp* app, const PkpObject* o, char* buf, size_t cap) {
    char code[24];
    snprintf(code, sizeof(code), "%s", o->code.len ? str(app, o->code) : "");
    const char* title = str(app, o->title);
    if(!code[0] || strncmp(title, code, strlen(code)) == 0) return title;
    snprintf(buf, cap, "%s %.130s", code, title);
    return buf;
}

/* Find: the query and its wheel, the match count in the header, the best three titles. */
static void render_find(UiApp* app) {
    PkpSearch* s = &app->search;
    char line[32], win[8], label[160];
    page_reset(app->page, "Find by name");
    page_set_title_icon(app->page, UiIconFind);
    PkpStatus st = search_ready(app);
    if(st != PKP_OK) {
        page_add(
            app->page,
            PageNote,
            st == PKP_E_MISSING ?
                "This pack has no search index. Rebuild it with a current compiler, or browse "
                "By chapter." :
            pkp_count(app->pack, PKP_SEC_OBJECTS) > PKP_SEARCH_MAX_OBJECTS ?
                "This pack is too large to search on the Flipper. Browse By chapter instead." :
            st == PKP_E_IO ?
                "The search index could not be read. Check the SD card, then open Find again." :
                "This pack's search index is damaged. Rebuild the pack, or browse By chapter.",
            0);
        restore(app);
        return;
    }
    uint64_t mask = pkp_search_next(s);
    app->wheel = wheel_step(mask, app->wheel, 0);
    if(s->len) {
        snprintf(line, sizeof(line), "%u OK>", s->count); /* how many, and that OK lists them */
        page_set_position(app->page, line);
    }
    wheel_window(mask, app->wheel, win);
    snprintf(line, sizeof(line), "%s\t%s", s->query, win);
    page_add(app->page, PageWheel, line, 0);
    if(app->search_error != PKP_OK) {
        page_add(
            app->page,
            PageNote,
            "The search index could not be read. Check the SD card, then try again.",
            0);
    } else if(s->len == 0) {
        page_add(
            app->page,
            PageNote,
            "Up/Down: a letter. Right: add. Left: delete, hold Left: clear. OK: list all.",
            0);
    }
    uint16_t best[FIND_TOP];
    uint32_t n = pkp_search_results(s, 0, best, FIND_TOP);
    for(uint32_t k = 0; k < n; k++) {
        PkpObject o;
        if(pkp_object(app->pack, best[k], &o) == PKP_OK)
            page_add(app->page, PageItem, coded_title(app, &o, label, sizeof(label)), 0);
    }
    restore(app);
}

/* Every match, strong first, paged by forty like a chapter's objects. */
static void render_find_results(UiApp* app) {
    Frame* f = top(app);
    PkpSearch* s = &app->search;
    uint16_t found[PAGE_ROWS];
    char title[24], label[160];
    snprintf(title, sizeof(title), "Found: %s", s->query);
    page_reset(app->page, title);
    page_set_title_icon(app->page, UiIconFind);
    uint32_t pages = (s->count + PAGE_ROWS - 1) / PAGE_ROWS;
    paging(app, f->page, pages, true);
    uint32_t n = pkp_search_results(s, (uint32_t)f->page * PAGE_ROWS, found, PAGE_ROWS);
    for(uint32_t k = 0; k < n; k++) {
        PkpObject o;
        if(pkp_object(app->pack, found[k], &o) != PKP_OK) continue;
        page_add_row(
            app->page, 0, coded_title(app, &o, label, sizeof(label)), NULL, T_OBJECT | found[k]);
    }
    paging(app, f->page, pages, false);
    restore(app);
}

/* Find's keys turn the wheel, not the page: Up/Down a letter, Right adds it, Left deletes one
 * (held: all), OK lists the matches. Back, and every key where there is no search, go to the
 * page as anywhere else. */
static bool find_input(UiApp* app, PageKey key, PagePress press) {
    PkpSearch* s = &app->search;
    if(key == PageKeyBack || app->search_status != PKP_OK) return false;
    uint64_t mask = pkp_search_next(s);
    if(key == PageKeyOk) {
        if(press == PagePressShort && s->len) {
            push(app, (Frame){.screen = ScrFindResults});
            return true;
        }
        return true; /* held OK opens no menu over the wheel */
    }
    app->search_error = PKP_OK;
    if(key == PageKeyUp || key == PageKeyDown) {
        app->wheel = wheel_step(mask, app->wheel, key == PageKeyDown ? 1 : -1);
    } else if(key == PageKeyRight && mask && press != PagePressLong) {
        app->search_error = pkp_search_push(s, pkp_search_letter(app->wheel));
        app->wheel = 0; /* the first letter the longer query offers */
    } else if(key == PageKeyLeft && press == PagePressLong) {
        pkp_search_clear(s);
        app->wheel = 0;
    } else if(key == PageKeyLeft && s->len) {
        char last = s->query[s->len - 1];
        app->search_error = pkp_search_pop(s);
        app->wheel = (uint8_t)pkp_search_position(last); /* Right adds it again */
    }
    render(app);
    return true;
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
    case ScrAssess:
        render_assess(app);
        break;
    case ScrAssessResult:
        render_assess_result(app);
        break;
    case ScrSessions:
        render_sessions(app);
        break;
    case ScrSimStep:
        render_sim_step(app);
        break;
    case ScrSimResult:
        render_sim_result(app);
        break;
    case ScrSimOutcome:
        render_sim_outcome(app);
        break;
    case ScrScore:
        render_score(app);
        break;
    case ScrScoreResult:
        render_score_result(app);
        break;
    case ScrCalc:
        render_calc(app);
        break;
    case ScrFind:
        render_find(app);
        break;
    case ScrFindResults:
        render_find_results(app);
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

/* An assessment's scales: each axis item and the grades right after it (bounded). */
static void start_assess(UiApp* app, uint16_t obj) {
    while(app->depth > 1 && (top(app)->screen == ScrAssess || top(app)->screen == ScrAssessResult))
        app->depth--;
    PkpObject o;
    app->axes = 0;
    memset(app->grade, 0, sizeof(app->grade));
    if(pkp_object(app->pack, obj, &o) != PKP_OK) o.items_count = 0;
    for(uint32_t k = 0; k < o.items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o.items_first + k, &it) != PKP_OK) break;
        if(it.type == PKP_ITEM_AXIS && app->axes < MAX_AXES) {
            app->axis_item[app->axes] = o.items_first + k;
            app->axis_levels[app->axes++] = 0;
        } else if(
            it.type == PKP_ITEM_LEVEL && app->axes &&
            app->axis_levels[app->axes - 1] < MAX_LEVELS) {
            app->axis_levels[app->axes - 1]++;
        }
    }
    push(app, (Frame){.screen = ScrAssess, .obj = obj, .aux = 0});
}

/* A simulation's steps: the starting trust, each step's prompt item and its options. A run
 * restarted from its outcome first leaves the run's own screens. */
static void start_simulation(UiApp* app, uint16_t obj) {
    while(app->depth > 1 && (top(app)->screen == ScrSimStep || top(app)->screen == ScrSimResult ||
                             top(app)->screen == ScrSimOutcome))
        app->depth--;
    PkpObject o;
    app->sim_steps = 0;
    app->sim_trust0 = 50;
    memset(app->sim_pick, 0, sizeof(app->sim_pick));
    if(pkp_object(app->pack, obj, &o) != PKP_OK) o.items_count = 0;
    for(uint32_t k = 0; k < o.items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o.items_first + k, &it) != PKP_OK) break;
        if(it.type == PKP_ITEM_SIM_START) app->sim_trust0 = (int16_t)atoi(str(app, it.text));
        if(it.type == PKP_ITEM_SIM_STEP && app->sim_steps < MAX_STEPS) {
            app->sim_step_item[app->sim_steps] = o.items_first + k;
            app->sim_step_options[app->sim_steps++] = 0;
        } else if(
            it.type == PKP_ITEM_SIM_OPTION && app->sim_steps &&
            app->sim_step_options[app->sim_steps - 1] < MAX_OPTIONS) {
            app->sim_step_options[app->sim_steps - 1]++;
        }
    }
    push(app, (Frame){.screen = ScrSimStep, .obj = obj, .aux = 0});
}

/* A scorecard's setup, questions and bands; restarting from the result leaves the run's own
 * screens first. */
static void start_score(UiApp* app, uint16_t obj) {
    while(app->depth > 1 && (top(app)->screen == ScrScore || top(app)->screen == ScrScoreResult))
        app->depth--;
    PkpObject o;
    int base = 0, min = 0, max = 100;
    app->sc_questions = app->sc_bands = 0;
    memset(app->sc_pick, 0, sizeof(app->sc_pick));
    if(pkp_object(app->pack, obj, &o) != PKP_OK) o.items_count = 0;
    for(uint32_t k = 0; k < o.items_count; k++) {
        PkpItem it;
        if(pkp_item(app->pack, o.items_first + k, &it) != PKP_OK) break;
        if(it.type == PKP_ITEM_SCORE_SETUP) score_setup(str(app, it.text), &base, &min, &max);
        if(it.type == PKP_ITEM_SCORE_QUESTION && app->sc_questions < MAX_QUESTIONS) {
            app->sc_q_item[app->sc_questions] = o.items_first + k;
            app->sc_q_choices[app->sc_questions++] = 0;
        } else if(
            it.type == PKP_ITEM_SCORE_CHOICE && app->sc_questions &&
            app->sc_q_choices[app->sc_questions - 1] < MAX_OPTIONS) {
            app->sc_q_choices[app->sc_questions - 1]++;
        } else if(it.type == PKP_ITEM_SCORE_BAND && app->sc_bands < MAX_BANDS) {
            if(app->sc_bands++ == 0) app->sc_band_item = o.items_first + k;
        }
    }
    app->sc_base = (int16_t)base;
    app->sc_min = (int16_t)min;
    app->sc_max = (int16_t)max;
    push(app, (Frame){.screen = ScrScore, .obj = obj, .aux = 0});
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
        [HomeAssess] = PKP_CAP_ASSESS,
        [HomeSimulate] = PKP_CAP_SIMULATE,
        [HomeScore] = PKP_CAP_SCORE,
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
    case HomeAssess:
    case HomeSimulate:
    case HomeScore:
        push(app, (Frame){.screen = ScrFilter, .filter = filters[entry]});
        break;
    case HomeCalc:
        push(app, (Frame){.screen = ScrFilter, .filter = 0, .cat = app->calc_kind});
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
    case HomeSessions:
        push(app, (Frame){.screen = ScrSessions});
        break;
    case HomeForgetSessions:
        push(app, (Frame){.screen = ScrForget, .filter = 1});
        break;
    case HomeForgetSessionsNow:
        app->platform.session_forget(app->platform.ctx);
        app->depth--; /* back to Info, which now shows none */
        render(app);
        break;
    case HomeFind:
        if(search_ready(app) == PKP_OK) pkp_search_clear(&app->search);
        app->wheel = 0;
        app->search_error = PKP_OK;
        push(app, (Frame){.screen = ScrFind});
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

/* A stepper's value, one step left or right, wrapping through the choices; the screen redraws
 * at once with the focus kept, so a held key runs through the values in real time. */
static void adjust(UiApp* app, uint32_t target, int dir) {
    uint32_t k = T_ARG(target);
    switch(T_KIND(target)) {
    case T_AXIS:
        if(app->axis_levels[k])
            app->grade[k] =
                (uint8_t)((app->grade[k] + app->axis_levels[k] + dir) % app->axis_levels[k]);
        break;
    case T_SCORE_Q:
        if(app->sc_q_choices[k])
            app->sc_pick[k] =
                (uint8_t)((app->sc_pick[k] + app->sc_q_choices[k] + dir) % app->sc_q_choices[k]);
        break;
    default: /* the calculator's steppers: the confidence to add, or the prior */
        if(k == CALC_ADD)
            app->calc_pick =
                (uint8_t)((app->calc_pick + app->calc.n_choices + dir) % app->calc.n_choices);
        else
            calc_step_prior(app, dir);
        break;
    }
    top(app)->sel = target;
    render(app);
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
    case T_ASSESS:
        start_assess(app, f->obj);
        break;
    case T_AXIS:
    case T_SCORE_Q:
        adjust(app, target, 1); /* OK steps forward, like Right */
        break;
    case T_GRADE_DONE:
        push(app, (Frame){.screen = ScrAssessResult, .obj = f->obj});
        break;
    case T_SAVE:
        save_session(app);
        break;
    case T_SIMULATE:
        start_simulation(app, f->obj);
        break;
    case T_SCORE:
        start_score(app, f->obj);
        break;
    case T_SCORE_DONE:
        push(app, (Frame){.screen = ScrScoreResult, .obj = f->obj});
        break;
    case T_CALC_OPEN:
        start_calc(app, f->obj);
        break;
    case T_CALC:
        calc_action(app, T_ARG(target));
        break;
    case T_SIM_PICK:
        app->sim_pick[f->aux] = (uint8_t)T_ARG(target);
        push(app, (Frame){.screen = ScrSimResult, .obj = f->obj, .aux = f->aux});
        break;
    case T_SIM_NEXT:
        if(f->aux + 1 < app->sim_steps)
            push(app, (Frame){.screen = ScrSimStep, .obj = f->obj, .aux = f->aux + 1});
        else
            push(app, (Frame){.screen = ScrSimOutcome, .obj = f->obj});
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
    app->search_open = false; /* Find opens the new pack's index when it is first needed */
    if(status == PKP_OK) {
        /* one pass over the object index for the home counts (sequential, cached reads) */
        uint32_t t0 = app->platform.tick_ms(app->platform.ctx);
        for(uint32_t i = 0; i < pkp_count(pack, PKP_SEC_OBJECTS); i++) {
            PkpObject o;
            if(pkp_object(pack, i, &o) != PKP_OK) continue;
            for(unsigned bit = 1; bit <= PKP_CAP_SCORE; bit <<= 1) { /* 128 << 1 wraps a uint8 */
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
    /* calculators are objects of the kind "algorithm" */
    for(uint32_t k = 1; status == PKP_OK && k <= pkp_count(pack, PKP_SEC_KINDS) && k < KIND_CAP;
        k++) {
        char name[16];
        if(pkp_kind_name(pack, (uint8_t)k, name, sizeof(name)) == PKP_OK &&
           strcmp(name, "algorithm") == 0)
            app->calc_kind = (uint8_t)k;
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
    if(top(app)->screen == ScrFind && find_input(app, key, press)) return true;
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
    case PageActAdjust:
        adjust(app, act.target, act.dir);
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
