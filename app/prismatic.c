/* Prismatic Pocket for Flipper Zero.
 *
 * A generic runtime over a knowledge pack (.pkp) on the SD card. No screen knows any concept:
 * categories, objects, articles, the actions an article links to (detector, quiz, chapters,
 * related and confusable objects) and every text are derived from the pack. A new object with
 * existing capabilities needs no change here.
 *
 * Screens: submenus for lists, a structured reader (app/reader.c) for articles and results,
 * the reader with a button bar for the detector and quiz. Navigation is a bounded frame stack: Back pops one frame
 * and returns to the remembered selection; Back on Home exits. The detector only counts the
 * operator's marks (observed / not observed / unknown / n/a); it never scores or diagnoses.
 */
#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/loading.h>
#include <gui/modules/submenu.h>
#include <storage/storage.h>

#include "core/pkp.h"
#include "core/pocket.h"
#include "reader.h"
#include "version.h"

#define TAG         "Prismatic"
#define PACK_DIR    EXT_PATH("apps_data/prismatic")
#define PAGE        40u
#define MAX_MARKERS 64u
#define STACK       24u
#define SCRATCH     (PKP_MAX_STRING + 1u)

/* Reader link targets: kind in the top byte, argument below. */
#define LINK_OBJECT    (1u << 24)
#define LINK_CHAPTER   (2u << 24) /* item offset inside the current object */
#define LINK_DETECT    (3u << 24)
#define LINK_QUIZ      (4u << 24)
#define LINK_NEXT_QUIZ (5u << 24) /* argument: quiz kind */
#define LINK_CATEGORY  (6u << 24)
#define LINK_KIND(t)   ((t) & 0xFF000000u)
#define LINK_ARG(t)    ((t) & 0x00FFFFFFu)

typedef enum {
    ViewMenu,
    ViewReader,
    ViewWidget,
    ViewLoading,
} ViewId;

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
} Screen;

typedef struct {
    uint8_t screen;
    uint8_t filter; /* capability bit for ScrFilter */
    uint16_t cat;
    uint16_t page;
    uint16_t obj;
    uint32_t aux; /* chapter item offset, quiz index */
    uint32_t sel; /* submenu selection to restore */
    uint8_t pick; /* quiz answer given */
} Frame;

enum {
    EvHome = 1,
    EvCategory,
    EvObject,
    EvPagePrev,
    EvPageNext,
    EvMark,
    EvQuizOption,
    EvQuizAnswer,
    EvLink,
};
#define EV(a, i) (((uint32_t)(a) << 24) | ((i) & 0xFFFFFFu))

enum {
    HomeBrowse,
    HomeDetect,
    HomeApply,
    HomeCompare,
    HomeTrainDefinition,
    HomeTrainExample,
    HomeInfo,
    HomeAbout,
};

typedef struct {
    File* file;
    uint32_t block_at;
    uint32_t block_len;
    uint8_t block[512];
    uint32_t reads;
} PackFile;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* vd;
    Submenu* menu;
    Reader* reader;
    Loading* loading;
    FuriString* text;
    char* scratch;

    PackFile pf;
    PkpReader pack;
    PkpStatus pack_status;
    FuriString* pack_path;
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
    uint32_t pending_link;
} App;

/* ------------------------------------------------------------------ pack I/O */

static bool pack_read(void* ctx, uint32_t offset, void* buf, uint32_t len) {
    PackFile* pf = ctx;
    if(len <= sizeof(pf->block)) {
        if(offset >= pf->block_at && offset + len <= pf->block_at + pf->block_len) {
            memcpy(buf, pf->block + (offset - pf->block_at), len);
            return true;
        }
        uint32_t at = offset & ~(uint32_t)(sizeof(pf->block) - 1);
        if(offset + len <= at + sizeof(pf->block)) {
            pf->reads++;
            if(!storage_file_seek(pf->file, at, true)) return false;
            size_t got = storage_file_read(pf->file, pf->block, sizeof(pf->block));
            pf->block_at = at;
            pf->block_len = (uint32_t)got;
            if(offset + len > at + got) return false;
            memcpy(buf, pf->block + (offset - at), len);
            return true;
        }
    }
    pf->reads++;
    if(!storage_file_seek(pf->file, offset, true)) return false;
    return storage_file_read(pf->file, buf, len) == len;
}

static uint8_t cap_index(uint8_t bit) {
    uint8_t i = 0;
    while(bit > 1) {
        bit >>= 1;
        i++;
    }
    return i;
}

static void pack_open(App* app) {
    app->pack_status = PKP_E_MISSING;
    furi_string_reset(app->pack_path);
    File* dir = storage_file_alloc(app->storage);
    char name[128];
    FuriString* best = furi_string_alloc();
    if(storage_dir_open(dir, PACK_DIR)) {
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            size_t n = strlen(name);
            if(!file_info_is_dir(&info) && n > 4 && strcmp(name + n - 4, ".pkp") == 0 &&
               (furi_string_empty(best) || strcmp(name, furi_string_get_cstr(best)) < 0)) {
                furi_string_set(best, name);
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    if(!furi_string_empty(best)) {
        furi_string_printf(app->pack_path, "%s/%s", PACK_DIR, furi_string_get_cstr(best));
        app->pf.file = storage_file_alloc(app->storage);
        app->pack_status = PKP_E_IO;
        if(storage_file_open(
               app->pf.file, furi_string_get_cstr(app->pack_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint32_t size = (uint32_t)storage_file_size(app->pf.file);
            uint32_t t0 = furi_get_tick();
            app->pack_status = pkp_open(&app->pack, pack_read, &app->pf, size);
            app->open_ms = furi_get_tick() - t0;
        }
    }
    furi_string_free(best);
    if(app->pack_status != PKP_OK) return;
    /* One pass over the object index for the home counts (sequential, cached reads). */
    uint32_t t0 = furi_get_tick();
    for(uint32_t i = 0; i < pkp_count(&app->pack, PKP_SEC_OBJECTS); i++) {
        PkpObject o;
        if(pkp_object(&app->pack, i, &o) != PKP_OK) continue;
        for(uint8_t bit = 1; bit <= PKP_CAP_COMPARE; bit <<= 1) {
            if(o.caps & bit) app->cap_count[cap_index(bit)]++;
        }
    }
    for(uint32_t i = 0; i < pkp_count(&app->pack, PKP_SEC_QUIZ); i++) {
        PkpQuiz q;
        if(pkp_quiz(&app->pack, i, &q) == PKP_OK && q.kind < 3) app->quiz_kind_count[q.kind]++;
    }
    app->scan_ms = furi_get_tick() - t0;
    FURI_LOG_I(
        TAG,
        "pack open %lu ms, scan %lu ms",
        (unsigned long)app->open_ms,
        (unsigned long)app->scan_ms);
}

static const char* str(App* app, PkpStr s) {
    pkp_string(&app->pack, s, app->scratch, SCRATCH);
    return app->scratch;
}

/* ------------------------------------------------------------------ helpers */

static void menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->vd, index);
}

static void link_cb(void* ctx, uint32_t target) {
    App* app = ctx;
    app->pending_link = target;
    view_dispatcher_send_custom_event(app->vd, EV(EvLink, 0));
}

static Frame* top(App* app) {
    return &app->stack[app->depth - 1];
}

static void render(App* app);

static void push(App* app, Frame f) {
    if(app->depth == STACK) {
        memmove(app->stack, app->stack + 1, sizeof(Frame) * (STACK - 1));
        app->depth--;
    }
    app->stack[app->depth++] = f;
    render(app);
}

static void label_of(App* app, PkpStr s, char* out, size_t cap) {
    strlcpy(out, str(app, s), cap);
}

static uint32_t count_items(App* app, const PkpObject* o, uint8_t type) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o->items_first + k, &it) == PKP_OK && it.type == type) n++;
    }
    return n;
}

/* Adds every item of `type` as reader blocks of `style`; returns how many. */
static uint32_t add_items(App* app, const PkpObject* o, uint8_t type, ReaderStyle style) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o->items_first + k, &it) != PKP_OK || it.type != type) continue;
        reader_add(app->reader, style, str(app, it.text), 0);
        n++;
    }
    return n;
}

/* Adds links to related objects of relation type `rel` (1-based), or all when 0. */
static uint32_t add_relation_links(App* app, const PkpObject* o, uint8_t rel, bool except) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->rel_count; k++) {
        PkpRelation r;
        PkpObject t;
        if(pkp_relation(&app->pack, o->rel_first + k, &r) != PKP_OK) continue;
        if(rel && ((r.type == rel) == except)) continue;
        if(pkp_object(&app->pack, r.target, &t) != PKP_OK) continue;
        reader_add(app->reader, ReaderLink, str(app, t.title), LINK_OBJECT | r.target);
        n++;
    }
    return n;
}

static uint8_t relation_type(App* app, const char* name) {
    char buf[32];
    for(uint32_t i = 1; i <= pkp_count(&app->pack, PKP_SEC_RELNAMES); i++) {
        if(pkp_relation_name(&app->pack, (uint8_t)i, buf, sizeof(buf)) == PKP_OK &&
           strcmp(buf, name) == 0)
            return (uint8_t)i;
    }
    return 0;
}

static uint16_t quiz_subject(const PkpQuiz* q) {
    return q->kind == PKP_QUIZ_DEFINITION ? q->options[q->answer] : q->options[0];
}

/* First quiz entry of `kind` for object `obj`, or -1. Entries are ordered by subject. */
static int32_t quiz_for(App* app, uint16_t obj, uint8_t kind) {
    uint32_t lo = 0, hi = pkp_count(&app->pack, PKP_SEC_QUIZ);
    PkpQuiz q;
    while(lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if(pkp_quiz(&app->pack, mid, &q) != PKP_OK) return -1;
        if(quiz_subject(&q) < obj)
            lo = mid + 1;
        else
            hi = mid;
    }
    for(uint32_t i = lo; i < pkp_count(&app->pack, PKP_SEC_QUIZ); i++) {
        if(pkp_quiz(&app->pack, i, &q) != PKP_OK || quiz_subject(&q) != obj) break;
        if(!kind || q.kind == kind) return (int32_t)i;
    }
    return -1;
}

static int32_t quiz_random(App* app, uint8_t kind) {
    uint32_t n = pkp_count(&app->pack, PKP_SEC_QUIZ);
    PkpQuiz q;
    for(int tries = 0; n && tries < 64; tries++) {
        uint32_t i = furi_hal_random_get() % n;
        if(pkp_quiz(&app->pack, i, &q) == PKP_OK && q.kind == kind) return (int32_t)i;
    }
    return -1;
}

static void restore_selection(App* app) {
    if(top(app)->sel) submenu_set_selected_item(app->menu, top(app)->sel);
}

/* ------------------------------------------------------------------ list screens */

static void render_home(App* app) {
    char label[48];
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Prismatic Pocket");
    if(app->pack_status == PKP_OK) {
        snprintf(
            label,
            sizeof(label),
            "Browse (%lu)",
            (unsigned long)pkp_count(&app->pack, PKP_SEC_OBJECTS));
        submenu_add_item(app->menu, label, EV(EvHome, HomeBrowse), menu_cb, app);
        snprintf(
            label, sizeof(label), "Detectors (%u)", app->cap_count[cap_index(PKP_CAP_DETECT)]);
        submenu_add_item(app->menu, label, EV(EvHome, HomeDetect), menu_cb, app);
        snprintf(
            label, sizeof(label), "What to do (%u)", app->cap_count[cap_index(PKP_CAP_APPLY)]);
        submenu_add_item(app->menu, label, EV(EvHome, HomeApply), menu_cb, app);
        snprintf(
            label, sizeof(label), "Confusables (%u)", app->cap_count[cap_index(PKP_CAP_COMPARE)]);
        submenu_add_item(app->menu, label, EV(EvHome, HomeCompare), menu_cb, app);
        snprintf(
            label,
            sizeof(label),
            "Quiz: terms (%lu)",
            (unsigned long)app->quiz_kind_count[PKP_QUIZ_DEFINITION]);
        submenu_add_item(app->menu, label, EV(EvHome, HomeTrainDefinition), menu_cb, app);
        snprintf(
            label,
            sizeof(label),
            "Quiz: manip.? (%lu)",
            (unsigned long)app->quiz_kind_count[PKP_QUIZ_TRAINER]);
        submenu_add_item(app->menu, label, EV(EvHome, HomeTrainExample), menu_cb, app);
    }
    submenu_add_item(app->menu, "Pack & device info", EV(EvHome, HomeInfo), menu_cb, app);
    submenu_add_item(app->menu, "About", EV(EvHome, HomeAbout), menu_cb, app);
    restore_selection(app);
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_categories(App* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Categories");
    for(uint32_t i = 0; i < pkp_count(&app->pack, PKP_SEC_CATEGORIES); i++) {
        PkpCategory c;
        if(pkp_category(&app->pack, i, &c) != PKP_OK) continue;
        char label[64];
        snprintf(label, sizeof(label), "%s (%u)", str(app, c.name), c.count);
        submenu_add_item(app->menu, label, EV(EvCategory, i), menu_cb, app);
    }
    restore_selection(app);
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void header_with_page(App* app, const char* name, uint32_t page, uint32_t pages) {
    char head[40], num[12];
    snprintf(
        num, sizeof(num), " %lu/%lu", (unsigned long)page + 1, (unsigned long)(pages ? pages : 1));
    size_t room = sizeof(head) - strlen(num) - 1;
    if(room > 18) room = 18;
    strlcpy(head, name, room + 1);
    strlcat(head, num, sizeof(head));
    submenu_set_header(app->menu, head);
}

static void render_objects(App* app) {
    Frame* f = top(app);
    PkpCategory c;
    submenu_reset(app->menu);
    if(pkp_category(&app->pack, f->cat, &c) != PKP_OK) return;
    uint32_t pages = (c.count + PAGE - 1) / PAGE;
    header_with_page(app, str(app, c.name), f->page, pages);
    if(f->page > 0) submenu_add_item(app->menu, "< previous", EV(EvPagePrev, 0), menu_cb, app);
    uint32_t from = (uint32_t)f->page * PAGE;
    for(uint32_t i = from; i < c.count && i < from + PAGE; i++) {
        PkpObject o;
        if(pkp_object(&app->pack, c.first + i, &o) != PKP_OK) continue;
        char label[64];
        label_of(app, o.title, label, sizeof(label));
        submenu_add_item(app->menu, label, EV(EvObject, c.first + i), menu_cb, app);
    }
    if(from + PAGE < c.count)
        submenu_add_item(app->menu, "next >", EV(EvPageNext, 0), menu_cb, app);
    restore_selection(app);
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

/* Objects with one capability, paged, in pack order (category, title). */
static void render_filter(App* app) {
    Frame* f = top(app);
    static const char* const names[] = {"All", "Detectors", "What to do", "Quiz", "Confusables"};
    uint32_t total = app->cap_count[cap_index(f->filter)];
    uint32_t pages = (total + PAGE - 1) / PAGE;
    submenu_reset(app->menu);
    header_with_page(app, names[cap_index(f->filter)], f->page, pages);
    if(f->page > 0) submenu_add_item(app->menu, "< previous", EV(EvPagePrev, 0), menu_cb, app);
    uint32_t skip = (uint32_t)f->page * PAGE, shown = 0, seen = 0;
    for(uint32_t i = 0; i < pkp_count(&app->pack, PKP_SEC_OBJECTS) && shown < PAGE; i++) {
        PkpObject o;
        if(pkp_object(&app->pack, i, &o) != PKP_OK || !(o.caps & f->filter)) continue;
        if(seen++ < skip) continue;
        char label[64];
        label_of(app, o.title, label, sizeof(label));
        submenu_add_item(app->menu, label, EV(EvObject, i), menu_cb, app);
        shown++;
    }
    if(skip + PAGE < total) submenu_add_item(app->menu, "next >", EV(EvPageNext, 0), menu_cb, app);
    restore_selection(app);
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

/* ------------------------------------------------------------------ reader screens */

static void render_article(App* app) {
    Frame* f = top(app);
    PkpObject o;
    reader_reset(app->reader);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) {
        reader_add(app->reader, ReaderNote, "This object could not be read.", 0);
        reader_show(app->reader);
        view_dispatcher_switch_to_view(app->vd, ViewReader);
        return;
    }
    char kind[24], meta[96];
    PkpCategory c;
    pkp_kind_name(&app->pack, o.kind, kind, sizeof(kind));
    reader_add(app->reader, ReaderTitle, str(app, o.title), 0);
    snprintf(meta, sizeof(meta), "%s", kind);
    if(o.code.len) {
        strlcat(meta, " - ", sizeof(meta));
        strlcat(meta, str(app, o.code), sizeof(meta));
    }
    reader_add(app->reader, ReaderMeta, meta, 0);
    reader_add(app->reader, ReaderParagraph, str(app, o.definition), 0);

    char label[48];
    uint32_t markers = count_items(app, &o, PKP_ITEM_INDICATOR);
    if(markers) {
        snprintf(label, sizeof(label), "Run the detector (%lu markers)", (unsigned long)markers);
        reader_add(app->reader, ReaderLink, label, LINK_DETECT);
    }
    if(o.caps & PKP_CAP_QUIZ) reader_add(app->reader, ReaderLink, "Take the quiz", LINK_QUIZ);

    if(markers) {
        reader_add(app->reader, ReaderHeading, "How to recognise it", 0);
        add_items(app, &o, PKP_ITEM_INDICATOR, ReaderBullet);
    }
    if(count_items(app, &o, PKP_ITEM_ACTION)) {
        reader_add(app->reader, ReaderHeading, "What to do", 0);
        add_items(app, &o, PKP_ITEM_ACTION, ReaderBullet);
    }
    if(count_items(app, &o, PKP_ITEM_ESCALATION)) {
        reader_add(app->reader, ReaderHeading, "When to escalate", 0);
        add_items(app, &o, PKP_ITEM_ESCALATION, ReaderBullet);
    }
    uint8_t confused = relation_type(app, "confused_with");
    if(count_items(app, &o, PKP_ITEM_ALTERNATIVE) || confused) {
        uint32_t before = count_items(app, &o, PKP_ITEM_ALTERNATIVE);
        if(before) {
            reader_add(app->reader, ReaderHeading, "Don't confuse with", 0);
            add_items(app, &o, PKP_ITEM_ALTERNATIVE, ReaderBullet);
        }
        if(confused) add_relation_links(app, &o, confused, false);
    }
    if(count_items(app, &o, PKP_ITEM_VALIDATION)) {
        reader_add(app->reader, ReaderHeading, "Validation tests", 0);
        add_items(app, &o, PKP_ITEM_VALIDATION, ReaderBullet);
    }
    if(count_items(app, &o, PKP_ITEM_HEADING)) {
        reader_add(app->reader, ReaderHeading, "Chapters", 0);
        for(uint32_t k = 0; k < o.items_count; k++) {
            PkpItem it;
            if(pkp_item(&app->pack, o.items_first + k, &it) == PKP_OK &&
               it.type == PKP_ITEM_HEADING) {
                reader_add(app->reader, ReaderLink, str(app, it.text), LINK_CHAPTER | k);
            }
        }
    }
    if(o.rel_count) {
        reader_add(app->reader, ReaderHeading, "See also", 0);
        add_relation_links(app, &o, confused, true);
    }
    if(pkp_category(&app->pack, o.category, &c) == PKP_OK) {
        char in[72];
        snprintf(in, sizeof(in), "In: %s", str(app, c.name));
        reader_add(app->reader, ReaderLink, in, LINK_CATEGORY | o.category);
    }
    reader_add(
        app->reader, ReaderNote, "Source: Kompendium. Markers describe; they never prove.", 0);
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_chapter(App* app) {
    Frame* f = top(app);
    PkpObject o;
    PkpItem it;
    reader_reset(app->reader);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK ||
       pkp_item(&app->pack, o.items_first + f->aux, &it) != PKP_OK)
        return;
    reader_add(app->reader, ReaderTitle, str(app, it.text), 0);
    reader_add(app->reader, ReaderMeta, str(app, o.title), 0);
    uint32_t k = f->aux + 1;
    for(; k < o.items_count; k++) {
        if(pkp_item(&app->pack, o.items_first + k, &it) != PKP_OK || it.type != PKP_ITEM_TEXT)
            break;
        reader_add(app->reader, ReaderParagraph, str(app, it.text), 0);
    }
    for(; k < o.items_count; k++) {
        if(pkp_item(&app->pack, o.items_first + k, &it) == PKP_OK && it.type == PKP_ITEM_HEADING) {
            char label[64];
            snprintf(label, sizeof(label), "Next: %s", str(app, it.text));
            reader_add(app->reader, ReaderLink, label, LINK_CHAPTER | k);
            break;
        }
    }
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_detect_result(App* app) {
    Frame* f = top(app);
    PkpObject o;
    reader_reset(app->reader);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return;
    PocketSummary s;
    pocket_summarize(app->marks, app->marker_count, &s);
    char line[48];
    reader_add(app->reader, ReaderTitle, "Your observations", 0);
    reader_add(app->reader, ReaderMeta, str(app, o.title), 0);
    snprintf(line, sizeof(line), "Observed: %u of %u markers", s.counts[POCKET_OBSERVED], s.total);
    reader_add(app->reader, ReaderParagraph, line, 0);
    static const uint8_t rest[] = {POCKET_NOT_OBSERVED, POCKET_UNKNOWN, POCKET_NOT_APPLICABLE};
    for(size_t i = 0; i < sizeof(rest); i++) {
        if(s.counts[rest[i]]) {
            snprintf(line, sizeof(line), "%s: %u", pocket_answer_name(rest[i]), s.counts[rest[i]]);
            reader_add(app->reader, ReaderBullet, line, 0);
        }
    }
    reader_add(
        app->reader,
        ReaderNote,
        "Marks only: not a score, not a probability, not a diagnosis of anyone.",
        0);
    uint8_t confused = relation_type(app, "confused_with");
    if(count_items(app, &o, PKP_ITEM_ALTERNATIVE) || confused) {
        reader_add(app->reader, ReaderHeading, "Rule out first", 0);
        add_items(app, &o, PKP_ITEM_ALTERNATIVE, ReaderBullet);
        if(confused) add_relation_links(app, &o, confused, false);
    }
    if(count_items(app, &o, PKP_ITEM_VALIDATION)) {
        reader_add(app->reader, ReaderHeading, "Validate", 0);
        add_items(app, &o, PKP_ITEM_VALIDATION, ReaderBullet);
    }
    if(count_items(app, &o, PKP_ITEM_ACTION)) {
        reader_add(app->reader, ReaderHeading, "Next steps", 0);
        add_items(app, &o, PKP_ITEM_ACTION, ReaderBullet);
    }
    reader_add(app->reader, ReaderLink, "Open the article", LINK_OBJECT | f->obj);
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_quiz_result(App* app) {
    Frame* f = top(app);
    PkpObject o;
    reader_reset(app->reader);
    if(pkp_quiz(&app->pack, f->aux, &app->quiz) != PKP_OK) return;
    uint16_t subject = quiz_subject(&app->quiz);
    pkp_object(&app->pack, subject, &o);
    bool ok = pocket_quiz_correct(&app->quiz, f->pick);
    reader_add(app->reader, ReaderTitle, ok ? "Correct" : "Not quite", 0);
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        reader_add(
            app->reader,
            ReaderMeta,
            app->quiz.answer ? "This example is benign." : "This example is manipulative.",
            0);
        reader_add(app->reader, ReaderNote, str(app, app->quiz.prompt), 0);
        reader_add(app->reader, ReaderHeading, "Why", 0);
        reader_add(app->reader, ReaderParagraph, str(app, app->quiz.explanation), 0);
        reader_add(app->reader, ReaderHeading, "A good response", 0);
        reader_add(app->reader, ReaderParagraph, str(app, app->quiz.response), 0);
    } else {
        reader_add(app->reader, ReaderMeta, "The term was:", 0);
        reader_add(app->reader, ReaderHeading, str(app, o.title), 0);
        reader_add(app->reader, ReaderParagraph, str(app, o.definition), 0);
    }
    reader_add(app->reader, ReaderLink, str(app, o.title), LINK_OBJECT | subject);
    reader_add(app->reader, ReaderLink, "Next question", LINK_NEXT_QUIZ | app->quiz.kind);
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_info(App* app) {
    char line[96];
    reader_reset(app->reader);
    reader_add(app->reader, ReaderTitle, "Pack & device", 0);
    if(app->pack_status != PKP_OK) {
        reader_add(app->reader, ReaderHeading, "No usable knowledge pack", 0);
        reader_add(
            app->reader,
            ReaderParagraph,
            app->pack_status == PKP_E_MISSING ? "None found." : pkp_status_text(app->pack_status),
            0);
        reader_add(
            app->reader,
            ReaderParagraph,
            "Copy a .pkp file to SD/apps_data/prismatic/ (computer: just deploy-pack).",
            0);
    } else {
        static const char* const labels[PKP_META_COUNT] = {
            "Pack", "Title", "Version", "Source", "Commit", "Built by", "Licence"};
        PkpStr s;
        for(uint32_t i = 0; i < PKP_META_COUNT; i++) {
            if(pkp_meta(&app->pack, i, &s) == PKP_OK) {
                snprintf(line, sizeof(line), "%s: ", labels[i]);
                strlcat(line, str(app, s), sizeof(line));
                reader_add(app->reader, i == 0 ? ReaderHeading : ReaderBullet, line, 0);
            }
        }
        reader_add(app->reader, ReaderHeading, "Contents", 0);
        snprintf(
            line,
            sizeof(line),
            "%lu objects in %lu categories",
            (unsigned long)pkp_count(&app->pack, PKP_SEC_OBJECTS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_CATEGORIES));
        reader_add(app->reader, ReaderBullet, line, 0);
        snprintf(
            line,
            sizeof(line),
            "%lu items, %lu links, %lu quiz items",
            (unsigned long)pkp_count(&app->pack, PKP_SEC_ITEMS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_RELATIONS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_QUIZ));
        reader_add(app->reader, ReaderBullet, line, 0);
        reader_add(app->reader, ReaderHeading, "Measured on this device", 0);
        snprintf(line, sizeof(line), "Open + index CRC: %lu ms", (unsigned long)app->open_ms);
        reader_add(app->reader, ReaderBullet, line, 0);
        snprintf(line, sizeof(line), "Index scan: %lu ms", (unsigned long)app->scan_ms);
        reader_add(app->reader, ReaderBullet, line, 0);
        snprintf(line, sizeof(line), "Pack size: %lu B", (unsigned long)app->pack.size);
        reader_add(app->reader, ReaderBullet, line, 0);
    }
    snprintf(
        line,
        sizeof(line),
        "Free heap: %lu B (largest %lu B)",
        (unsigned long)memmgr_get_free_heap(),
        (unsigned long)memmgr_heap_get_max_free_block());
    reader_add(app->reader, ReaderBullet, line, 0);
    snprintf(line, sizeof(line), "App v%s", PRISMATIC_VERSION);
    reader_add(app->reader, ReaderBullet, line, 0);
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_about(App* app) {
    char line[48];
    reader_reset(app->reader);
    reader_add(app->reader, ReaderTitle, "Prismatic Pocket", 0);
    snprintf(line, sizeof(line), "v%s - offline - evidence-first", PRISMATIC_VERSION);
    reader_add(app->reader, ReaderMeta, line, 0);
    reader_add(
        app->reader,
        ReaderParagraph,
        "Every screen is derived from the knowledge pack on the SD card: articles, "
        "detectors, what-to-do steps, confusables and quizzes.",
        0);
    reader_add(app->reader, ReaderHeading, "Controls", 0);
    reader_add(app->reader, ReaderBullet, "Up / Down: scroll", 0);
    reader_add(app->reader, ReaderBullet, "Left / Right: previous / next link", 0);
    reader_add(app->reader, ReaderBullet, "OK: open the link", 0);
    reader_add(app->reader, ReaderBullet, "Back: one step back", 0);
    reader_add(
        app->reader,
        ReaderNote,
        "The detector counts what you mark. It does not score, rank or diagnose anyone.",
        0);
    reader_add(app->reader, ReaderMeta, "github.com/korczis/prismatic-flipperzero", 0);
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

/* ------------------------------------------------------------------ detector and quiz */

static void reader_button(void* ctx, uint8_t button, bool long_press) {
    App* app = ctx;
    Frame* f = top(app);
    if(f->screen == ScrDetect) {
        static const uint8_t marks[3] = {POCKET_NOT_OBSERVED, POCKET_UNKNOWN, POCKET_OBSERVED};
        uint8_t mark = (button == 1 && long_press) ? POCKET_NOT_APPLICABLE : marks[button];
        view_dispatcher_send_custom_event(app->vd, EV(EvMark, mark));
    } else if(f->screen == ScrQuiz && !long_press) {
        if(app->quiz.kind == PKP_QUIZ_TRAINER && button != 1) {
            view_dispatcher_send_custom_event(app->vd, EV(EvQuizAnswer, button == 0 ? 0 : 1));
        } else if(app->quiz.kind == PKP_QUIZ_DEFINITION && button == 1) {
            view_dispatcher_send_custom_event(app->vd, EV(EvQuizOption, 0));
        }
    }
}

static void render_detect(App* app) {
    PkpItem it;
    PkpObject o;
    char head[32];
    reader_reset(app->reader);
    if(pkp_item(&app->pack, app->marker_first + app->marker_at, &it) != PKP_OK ||
       pkp_object(&app->pack, top(app)->obj, &o) != PKP_OK)
        return;
    snprintf(head, sizeof(head), "Marker %u of %u", app->marker_at + 1, app->marker_count);
    reader_add(app->reader, ReaderHeading, head, 0);
    reader_add(app->reader, ReaderParagraph, str(app, it.text), 0);
    reader_add(app->reader, ReaderMeta, "Have you observed this?", 0);
    reader_add(app->reader, ReaderNote, "Hold OK: not applicable", 0);
    reader_set_buttons(app->reader, "No", "?", "Yes");
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_quiz(App* app) {
    reader_reset(app->reader);
    if(pkp_quiz(&app->pack, top(app)->aux, &app->quiz) != PKP_OK) return;
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        reader_add(app->reader, ReaderHeading, "Manipulative or benign?", 0);
        reader_add(app->reader, ReaderMeta, str(app, app->quiz.context), 0);
        reader_add(app->reader, ReaderNote, str(app, app->quiz.prompt), 0);
        reader_set_buttons(app->reader, "Manip.", NULL, "Benign");
    } else {
        reader_add(app->reader, ReaderHeading, "Which term is this?", 0);
        reader_add(app->reader, ReaderParagraph, str(app, app->quiz.prompt), 0);
        reader_set_buttons(app->reader, NULL, "Choose", NULL);
    }
    reader_show(app->reader);
    view_dispatcher_switch_to_view(app->vd, ViewReader);
}

static void render_quiz_options(App* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Which term?");
    for(uint8_t i = 0; i < 4; i++) {
        PkpObject o;
        if(pkp_object(&app->pack, app->quiz.options[i], &o) != PKP_OK) continue;
        char label[64];
        label_of(app, o.title, label, sizeof(label));
        submenu_add_item(app->menu, label, EV(EvQuizAnswer, i), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render(App* app) {
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
    default:
        render_about(app);
        break;
    }
}

/* ------------------------------------------------------------------ events */

static void start_detect(App* app, uint16_t obj) {
    PkpObject o;
    if(pkp_object(&app->pack, obj, &o) != PKP_OK) return;
    app->marker_count = 0;
    app->marker_at = 0;
    for(uint32_t k = 0; k < o.items_count && app->marker_count < MAX_MARKERS; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o.items_first + k, &it) != PKP_OK) break;
        if(it.type == PKP_ITEM_INDICATOR) {
            if(app->marker_count == 0) app->marker_first = o.items_first + k;
            app->marks[app->marker_count++] = POCKET_UNANSWERED;
        } else if(app->marker_count) {
            break; /* indicators are contiguous: the encoder writes them first */
        }
    }
    if(app->marker_count) push(app, (Frame){.screen = ScrDetect, .obj = obj});
}

static void start_quiz(App* app, int32_t q, uint16_t obj) {
    if(q >= 0) push(app, (Frame){.screen = ScrQuiz, .aux = (uint32_t)q, .obj = obj});
}

static void follow_link(App* app, uint32_t target) {
    Frame* f = top(app);
    switch(LINK_KIND(target)) {
    case LINK_OBJECT:
        push(app, (Frame){.screen = ScrArticle, .obj = (uint16_t)LINK_ARG(target)});
        break;
    case LINK_CHAPTER:
        push(app, (Frame){.screen = ScrChapter, .obj = f->obj, .aux = LINK_ARG(target)});
        break;
    case LINK_DETECT:
        start_detect(app, f->obj);
        break;
    case LINK_QUIZ:
        start_quiz(app, quiz_for(app, f->obj, 0), f->obj);
        break;
    case LINK_CATEGORY:
        push(app, (Frame){.screen = ScrObjects, .cat = (uint16_t)LINK_ARG(target)});
        break;
    case LINK_NEXT_QUIZ:
        app->depth--; /* replace the finished question */
        start_quiz(app, quiz_random(app, (uint8_t)LINK_ARG(target)), 0);
        break;
    default:
        break;
    }
}

static bool on_custom(void* ctx, uint32_t event) {
    App* app = ctx;
    uint32_t action = event >> 24, arg = event & 0xFFFFFFu;
    Frame* f = top(app);
    if(action != EvLink && action != EvMark) f->sel = event; /* restore on the way back */
    switch(action) {
    case EvHome:
        if(arg == HomeBrowse)
            push(app, (Frame){.screen = ScrCategories});
        else if(arg == HomeDetect)
            push(app, (Frame){.screen = ScrFilter, .filter = PKP_CAP_DETECT});
        else if(arg == HomeApply)
            push(app, (Frame){.screen = ScrFilter, .filter = PKP_CAP_APPLY});
        else if(arg == HomeCompare)
            push(app, (Frame){.screen = ScrFilter, .filter = PKP_CAP_COMPARE});
        else if(arg == HomeInfo)
            push(app, (Frame){.screen = ScrInfo});
        else if(arg == HomeAbout)
            push(app, (Frame){.screen = ScrAbout});
        else
            start_quiz(
                app,
                quiz_random(
                    app, arg == HomeTrainDefinition ? PKP_QUIZ_DEFINITION : PKP_QUIZ_TRAINER),
                0);
        break;
    case EvCategory:
        push(app, (Frame){.screen = ScrObjects, .cat = (uint16_t)arg});
        break;
    case EvPagePrev:
    case EvPageNext:
        f->page = (uint16_t)(f->page + (action == EvPageNext ? 1 : -1));
        f->sel = 0;
        render(app);
        break;
    case EvObject:
        push(app, (Frame){.screen = ScrArticle, .obj = (uint16_t)arg});
        break;
    case EvLink:
        follow_link(app, app->pending_link);
        break;
    case EvMark:
        app->marks[app->marker_at] = (uint8_t)arg;
        if(++app->marker_at < app->marker_count) {
            render_detect(app);
        } else {
            f->screen = ScrDetectResult; /* the result replaces the questions */
            render(app);
        }
        break;
    case EvQuizOption:
        push(app, (Frame){.screen = ScrQuizOptions, .aux = f->aux, .obj = f->obj});
        break;
    case EvQuizAnswer:
        if(f->screen == ScrQuizOptions) {
            app->depth--; /* the result replaces the question and its options */
            f = top(app);
        }
        f->screen = ScrQuizResult;
        f->pick = (uint8_t)arg;
        render(app);
        break;
    default:
        return false;
    }
    return true;
}

static bool on_back(void* ctx) {
    App* app = ctx;
    if(app->depth <= 1) {
        view_dispatcher_stop(app->vd);
        return true;
    }
    app->depth--;
    render(app);
    return true;
}

/* ------------------------------------------------------------------ lifecycle */

int32_t prismatic_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->text = furi_string_alloc();
    app->pack_path = furi_string_alloc();
    app->scratch = malloc(SCRATCH);
    app->menu = submenu_alloc();
    app->reader = reader_alloc();
    reader_set_link_callback(app->reader, link_cb, app);
    reader_set_button_callback(app->reader, reader_button, app);
    app->loading = loading_alloc();
    app->vd = view_dispatcher_alloc();
    view_dispatcher_add_view(app->vd, ViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->vd, ViewReader, reader_get_view(app->reader));
    view_dispatcher_add_view(app->vd, ViewLoading, loading_get_view(app->loading));
    view_dispatcher_set_event_callback_context(app->vd, app);
    view_dispatcher_set_custom_event_callback(app->vd, on_custom);
    view_dispatcher_set_navigation_event_callback(app->vd, on_back);
    view_dispatcher_attach_to_gui(app->vd, app->gui, ViewDispatcherTypeFullscreen);

    view_dispatcher_switch_to_view(app->vd, ViewLoading);
    pack_open(app);
    app->stack[0] = (Frame){.screen = ScrHome};
    app->depth = 1;
    render(app);
    view_dispatcher_run(app->vd);

    view_dispatcher_remove_view(app->vd, ViewMenu);
    view_dispatcher_remove_view(app->vd, ViewReader);
    view_dispatcher_remove_view(app->vd, ViewLoading);
    view_dispatcher_free(app->vd);
    loading_free(app->loading);
    reader_free(app->reader);
    submenu_free(app->menu);
    if(app->pf.file) {
        storage_file_close(app->pf.file);
        storage_file_free(app->pf.file);
    }
    free(app->scratch);
    furi_string_free(app->pack_path);
    furi_string_free(app->text);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
