/* Prismatic Pocket for Flipper Zero.
 *
 * A generic runtime over a knowledge pack (.pkp) on the SD card. No screen knows any concept:
 * categories, objects, the actions offered for an object (definition, detector, applicator,
 * "don't confuse with", validation, chapters, related, quiz) and their texts are all derived
 * from the pack. A new object with existing capabilities needs no change here.
 *
 * Navigation: a bounded screen stack; every Back pops one frame, Back on Home exits.
 * The detector only counts the operator's marks (observed / not observed / unknown / n/a);
 * it never scores, ranks or diagnoses.
 */
#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/loading.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>

#include "core/pkp.h"
#include "core/pocket.h"
#include "version.h"

#define TAG "Prismatic"
#define PACK_DIR EXT_PATH("apps_data/prismatic")
#define PAGE 40u
#define MAX_MARKERS 64u
#define STACK 16u
#define SCRATCH (PKP_MAX_STRING + 1u)

typedef enum {
    ViewMenu,
    ViewText,
    ViewWidget,
    ViewLoading,
} ViewId;

typedef enum {
    ScrHome,
    ScrCategories,
    ScrObjects,
    ScrObject,
    ScrText,
    ScrChapters,
    ScrRelated,
    ScrDetect,
    ScrQuiz,
    ScrQuizOptions,
    ScrInfo,
} Screen;

typedef enum {
    TxtDefinition = 1,
    TxtApply,
    TxtCompare,
    TxtValidation,
    TxtChapter,
    TxtDetectResult,
    TxtQuizResult,
    TxtInfo,
    TxtAbout,
} TextKind;

typedef struct {
    uint8_t screen;
    uint8_t text;
    uint16_t cat;
    uint16_t page;
    uint16_t obj;
    uint32_t aux; /* chapter item index, quiz index, ... */
} Frame;

/* Custom events: action in the high half, argument in the low half. */
enum {
    EvHome = 1,
    EvCategory,
    EvObject,
    EvPagePrev,
    EvPageNext,
    EvObjectAction,
    EvChapter,
    EvRelated,
    EvMark,
    EvQuizOption,
    EvQuizNext,
    EvInfo,
};
#define EV(a, i) (((uint32_t)(a) << 16) | ((i) & 0xFFFFu))

enum {
    HomeBrowse,
    HomeTrainDefinition,
    HomeTrainExample,
    HomeInfo,
    HomeAbout,
};

enum {
    ObjDefinition,
    ObjDetect,
    ObjApply,
    ObjCompare,
    ObjValidation,
    ObjChapters,
    ObjRelated,
    ObjQuiz,
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
    TextBox* text_box;
    Widget* widget;
    Loading* loading;
    FuriString* text;
    char* scratch;

    PackFile pf;
    PkpReader pack;
    PkpStatus pack_status;
    FuriString* pack_path;
    uint32_t open_ms;

    Frame stack[STACK];
    uint8_t depth;

    uint8_t marks[MAX_MARKERS];
    uint16_t marker_count;
    uint16_t marker_at;
    uint32_t marker_first;

    PkpQuiz quiz;
    uint32_t quiz_index;
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

static void pack_open(App* app) {
    app->pack_status = PKP_E_IO;
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
    if(furi_string_empty(best)) {
        furi_string_free(best);
        app->pack_status = PKP_E_MISSING;
        return;
    }
    furi_string_printf(app->pack_path, "%s/%s", PACK_DIR, furi_string_get_cstr(best));
    furi_string_free(best);
    app->pf.file = storage_file_alloc(app->storage);
    app->pf.block_len = 0;
    if(!storage_file_open(
           app->pf.file, furi_string_get_cstr(app->pack_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        return;
    }
    uint32_t size = (uint32_t)storage_file_size(app->pf.file);
    uint32_t t0 = furi_get_tick();
    app->pack_status = pkp_open(&app->pack, pack_read, &app->pf, size);
    app->open_ms = furi_get_tick() - t0;
    FURI_LOG_I(TAG, "pack %s: %s in %lu ms", furi_string_get_cstr(app->pack_path),
               pkp_status_text(app->pack_status), (unsigned long)app->open_ms);
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

static void show_text(App* app) {
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->vd, ViewText);
}

/* Appends every item of `type` of the object as a bullet list; returns how many. */
static uint32_t cat_items(App* app, const PkpObject* o, uint8_t type, const char* bullet) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o->items_first + k, &it) != PKP_OK || it.type != type) continue;
        furi_string_cat_printf(app->text, "%s%s\n", bullet, str(app, it.text));
        n++;
    }
    return n;
}

static uint32_t count_items(App* app, const PkpObject* o, uint8_t type) {
    uint32_t n = 0;
    for(uint32_t k = 0; k < o->items_count; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o->items_first + k, &it) == PKP_OK && it.type == type) n++;
    }
    return n;
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
        if(quiz_subject(&q) < obj) lo = mid + 1;
        else hi = mid;
    }
    for(uint32_t i = lo; i < pkp_count(&app->pack, PKP_SEC_QUIZ); i++) {
        if(pkp_quiz(&app->pack, i, &q) != PKP_OK || quiz_subject(&q) != obj) break;
        if(q.kind == kind) return (int32_t)i;
    }
    return -1;
}

/* A random quiz entry of `kind` (bounded probing). */
static int32_t quiz_random(App* app, uint8_t kind) {
    uint32_t n = pkp_count(&app->pack, PKP_SEC_QUIZ);
    PkpQuiz q;
    for(int tries = 0; n && tries < 64; tries++) {
        uint32_t i = furi_hal_random_get() % n;
        if(pkp_quiz(&app->pack, i, &q) == PKP_OK && q.kind == kind) return (int32_t)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ screens */

static void render_home(App* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Prismatic Pocket");
    if(app->pack_status == PKP_OK) {
        submenu_add_item(app->menu, "Browse knowledge", EV(EvHome, HomeBrowse), menu_cb, app);
        submenu_add_item(
            app->menu, "Train: definitions", EV(EvHome, HomeTrainDefinition), menu_cb, app);
        submenu_add_item(
            app->menu, "Train: manip. or benign", EV(EvHome, HomeTrainExample), menu_cb, app);
    }
    submenu_add_item(app->menu, "Pack info", EV(EvHome, HomeInfo), menu_cb, app);
    submenu_add_item(app->menu, "About", EV(EvHome, HomeAbout), menu_cb, app);
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_categories(App* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Categories");
    for(uint32_t i = 0; i < pkp_count(&app->pack, PKP_SEC_CATEGORIES); i++) {
        PkpCategory c;
        if(pkp_category(&app->pack, i, &c) != PKP_OK) continue;
        snprintf(app->scratch, SCRATCH, "%s (%u)", str(app, c.name), c.count);
        char label[64];
        strlcpy(label, app->scratch, sizeof(label));
        submenu_add_item(app->menu, label, EV(EvCategory, i), menu_cb, app);
    }
    submenu_set_selected_item(app->menu, EV(EvCategory, top(app)->cat));
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_objects(App* app) {
    Frame* f = top(app);
    PkpCategory c;
    submenu_reset(app->menu);
    if(pkp_category(&app->pack, f->cat, &c) != PKP_OK) return;
    uint32_t pages = (c.count + PAGE - 1) / PAGE;
    snprintf(app->scratch, 32, "%u/%lu", f->page + 1, (unsigned long)(pages ? pages : 1));
    submenu_set_header(app->menu, app->scratch);
    if(f->page > 0) submenu_add_item(app->menu, "< previous page", EV(EvPagePrev, 0), menu_cb, app);
    uint32_t from = (uint32_t)f->page * PAGE;
    for(uint32_t i = from; i < c.count && i < from + PAGE; i++) {
        PkpObject o;
        if(pkp_object(&app->pack, c.first + i, &o) != PKP_OK) continue;
        char label[64];
        strlcpy(label, str(app, o.title), sizeof(label));
        submenu_add_item(app->menu, label, EV(EvObject, c.first + i), menu_cb, app);
    }
    if(from + PAGE < c.count) {
        submenu_add_item(app->menu, "next page >", EV(EvPageNext, 0), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_object(App* app) {
    Frame* f = top(app);
    PkpObject o;
    submenu_reset(app->menu);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return;
    char header[40];
    strlcpy(header, str(app, o.title), sizeof(header));
    submenu_set_header(app->menu, header);
    char label[40];
    submenu_add_item(app->menu, "Definition", EV(EvObjectAction, ObjDefinition), menu_cb, app);
    uint32_t n;
    if((n = count_items(app, &o, PKP_ITEM_INDICATOR))) {
        snprintf(label, sizeof(label), "Detect (%lu markers)", (unsigned long)n);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjDetect), menu_cb, app);
    }
    if((n = count_items(app, &o, PKP_ITEM_ACTION) + count_items(app, &o, PKP_ITEM_ESCALATION))) {
        snprintf(label, sizeof(label), "What to do (%lu)", (unsigned long)n);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjApply), menu_cb, app);
    }
    if((n = count_items(app, &o, PKP_ITEM_ALTERNATIVE))) {
        snprintf(label, sizeof(label), "Don't confuse (%lu)", (unsigned long)n);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjCompare), menu_cb, app);
    }
    if((n = count_items(app, &o, PKP_ITEM_VALIDATION))) {
        snprintf(label, sizeof(label), "Validation tests (%lu)", (unsigned long)n);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjValidation), menu_cb, app);
    }
    if((n = count_items(app, &o, PKP_ITEM_HEADING))) {
        snprintf(label, sizeof(label), "Read (%lu chapters)", (unsigned long)n);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjChapters), menu_cb, app);
    }
    if(o.rel_count) {
        snprintf(label, sizeof(label), "Related (%u)", o.rel_count);
        submenu_add_item(app->menu, label, EV(EvObjectAction, ObjRelated), menu_cb, app);
    }
    if(o.caps & PKP_CAP_QUIZ) {
        submenu_add_item(app->menu, "Quiz", EV(EvObjectAction, ObjQuiz), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_chapters(App* app) {
    Frame* f = top(app);
    PkpObject o;
    submenu_reset(app->menu);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return;
    submenu_set_header(app->menu, "Chapters");
    for(uint32_t k = 0; k < o.items_count; k++) {
        PkpItem it;
        if(pkp_item(&app->pack, o.items_first + k, &it) != PKP_OK || it.type != PKP_ITEM_HEADING)
            continue;
        char label[64];
        strlcpy(label, str(app, it.text), sizeof(label));
        submenu_add_item(app->menu, label, EV(EvChapter, k), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void render_related(App* app) {
    Frame* f = top(app);
    PkpObject o;
    submenu_reset(app->menu);
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return;
    submenu_set_header(app->menu, "Related");
    for(uint32_t k = 0; k < o.rel_count; k++) {
        PkpRelation rel;
        PkpObject t;
        if(pkp_relation(&app->pack, o.rel_first + k, &rel) != PKP_OK ||
           pkp_object(&app->pack, rel.target, &t) != PKP_OK)
            continue;
        char label[64];
        snprintf(
            label, sizeof(label), "%s%s",
            rel.type == 3 ? "~ " : "", /* confused_with first, marked */
            str(app, t.title));
        submenu_add_item(app->menu, label, EV(EvRelated, rel.target), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void detect_button(GuiButtonType button, InputType type, void* ctx) {
    App* app = ctx;
    if(type != InputTypeShort && type != InputTypeLong) return;
    uint8_t mark = POCKET_UNANSWERED;
    if(button == GuiButtonTypeLeft) mark = POCKET_NOT_OBSERVED;
    else if(button == GuiButtonTypeRight) mark = POCKET_OBSERVED;
    else if(button == GuiButtonTypeCenter) {
        mark = type == InputTypeLong ? POCKET_NOT_APPLICABLE : POCKET_UNKNOWN;
    }
    if(type == InputTypeLong && button != GuiButtonTypeCenter) return;
    view_dispatcher_send_custom_event(app->vd, EV(EvMark, mark));
}

static void render_detect_question(App* app) {
    PkpItem it;
    widget_reset(app->widget);
    if(pkp_item(&app->pack, app->marker_first + app->marker_at, &it) != PKP_OK) return;
    snprintf(app->scratch, 24, "%u/%u", app->marker_at + 1, app->marker_count);
    widget_add_string_element(app->widget, 127, 0, AlignRight, AlignTop, FontSecondary, app->scratch);
    furi_string_set_str(app->text, "Observed?\n");
    furi_string_cat_str(app->text, str(app, it.text));
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text));
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "No", detect_button, app);
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "?", detect_button, app);
    widget_add_button_element(app->widget, GuiButtonTypeRight, "Yes", detect_button, app);
    view_dispatcher_switch_to_view(app->vd, ViewWidget);
}

static void detect_result(App* app, const PkpObject* o) {
    PocketSummary s;
    pocket_summarize(app->marks, app->marker_count, &s);
    char sum[160];
    pocket_summary_text(&s, sum, sizeof(sum));
    furi_string_printf(app->text, "%s\n%s\n\n", str(app, o->title), sum);
    furi_string_cat_str(
        app->text,
        "Marks only: not a score, not a probability, not a diagnosis of anyone.\n\n");
    if(count_items(app, o, PKP_ITEM_ALTERNATIVE)) {
        furi_string_cat_str(app->text, "RULE OUT FIRST\n");
        cat_items(app, o, PKP_ITEM_ALTERNATIVE, "- ");
        furi_string_cat_str(app->text, "\n");
    }
    if(count_items(app, o, PKP_ITEM_VALIDATION)) {
        furi_string_cat_str(app->text, "VALIDATE\n");
        cat_items(app, o, PKP_ITEM_VALIDATION, "- ");
        furi_string_cat_str(app->text, "\n");
    }
    if(count_items(app, o, PKP_ITEM_ACTION)) {
        furi_string_cat_str(app->text, "NEXT STEPS\n");
        cat_items(app, o, PKP_ITEM_ACTION, "- ");
    }
    show_text(app);
}

static void quiz_button(GuiButtonType button, InputType type, void* ctx) {
    App* app = ctx;
    if(type != InputTypeShort) return;
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        uint8_t pick = button == GuiButtonTypeLeft ? 0 : button == GuiButtonTypeRight ? 1 : 9;
        if(pick < 2) view_dispatcher_send_custom_event(app->vd, EV(EvQuizOption, pick));
    } else if(button == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->vd, EV(EvQuizNext, 0));
    }
}

static void render_quiz(App* app) {
    widget_reset(app->widget);
    if(pkp_quiz(&app->pack, top(app)->aux, &app->quiz) != PKP_OK) return;
    furi_string_reset(app->text);
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        furi_string_cat_printf(app->text, "Context: %s\n", str(app, app->quiz.context));
        furi_string_cat_printf(app->text, "\"%s\"", str(app, app->quiz.prompt));
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text));
        widget_add_button_element(app->widget, GuiButtonTypeLeft, "Manip.", quiz_button, app);
        widget_add_button_element(app->widget, GuiButtonTypeRight, "Benign", quiz_button, app);
    } else {
        furi_string_cat_printf(app->text, "Which one is this?\n%s", str(app, app->quiz.prompt));
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text));
        widget_add_button_element(app->widget, GuiButtonTypeCenter, "Answer", quiz_button, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewWidget);
}

static void render_quiz_options(App* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Pick the term");
    for(uint8_t i = 0; i < 4; i++) {
        PkpObject o;
        if(pkp_object(&app->pack, app->quiz.options[i], &o) != PKP_OK) continue;
        char label[64];
        strlcpy(label, str(app, o.title), sizeof(label));
        submenu_add_item(app->menu, label, EV(EvQuizOption, i), menu_cb, app);
    }
    view_dispatcher_switch_to_view(app->vd, ViewMenu);
}

static void quiz_result(App* app, uint8_t pick) {
    bool ok = pocket_quiz_correct(&app->quiz, pick);
    PkpObject o;
    pkp_object(&app->pack, quiz_subject(&app->quiz), &o);
    furi_string_printf(app->text, "%s\n\n", ok ? "CORRECT" : "NOT QUITE");
    if(app->quiz.kind == PKP_QUIZ_TRAINER) {
        furi_string_cat_printf(
            app->text, "This example is %s.\nTopic: ",
            app->quiz.answer ? "benign" : "manipulative");
        furi_string_cat_printf(app->text, "%s\n\n", str(app, o.title));
        furi_string_cat_printf(app->text, "Why: %s\n\n", str(app, app->quiz.explanation));
        furi_string_cat_printf(app->text, "Response: %s\n", str(app, app->quiz.response));
    } else {
        furi_string_cat_printf(app->text, "Answer: %s\n\n", str(app, o.title));
        furi_string_cat_printf(app->text, "%s\n", str(app, o.definition));
    }
    furi_string_cat_str(app->text, "\n(Back: return)");
    show_text(app);
}

static void render_info(App* app) {
    furi_string_reset(app->text);
    if(app->pack_status != PKP_OK) {
        furi_string_printf(
            app->text,
            "No usable knowledge pack.\n%s\n\nCopy a .pkp file to\nSD/apps_data/prismatic/\n"
            "(on a computer: just deploy-pack)",
            app->pack_status == PKP_E_MISSING ? "none found" : pkp_status_text(app->pack_status));
    } else {
        PkpStr s;
        static const char* const labels[PKP_META_COUNT] = {
            "Pack", "Title", "Version", "Source", "Commit", "Built by", "Licence"};
        for(uint32_t i = 0; i < PKP_META_COUNT; i++) {
            if(pkp_meta(&app->pack, i, &s) == PKP_OK) {
                furi_string_cat_printf(app->text, "%s: %s\n", labels[i], str(app, s));
            }
        }
        furi_string_cat_printf(
            app->text, "\nObjects %lu, categories %lu\nItems %lu, relations %lu, quiz %lu\n",
            (unsigned long)pkp_count(&app->pack, PKP_SEC_OBJECTS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_CATEGORIES),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_ITEMS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_RELATIONS),
            (unsigned long)pkp_count(&app->pack, PKP_SEC_QUIZ));
        furi_string_cat_printf(
            app->text, "Size %lu B, opened in %lu ms\n", (unsigned long)app->pack.size,
            (unsigned long)app->open_ms);
        furi_string_cat_printf(app->text, "File: %s\n", furi_string_get_cstr(app->pack_path));
    }
    furi_string_cat_printf(
        app->text, "\nFree heap %lu B, largest block %lu B\nApp v%s",
        (unsigned long)memmgr_get_free_heap(), (unsigned long)memmgr_heap_get_max_free_block(),
        PRISMATIC_VERSION);
    show_text(app);
}

static void render_about(App* app) {
    furi_string_printf(
        app->text,
        "PRISMATIC POCKET v%s\n\nAn offline, evidence-first epistemic toolkit. Every screen is "
        "derived from the knowledge pack on the SD card.\n\nThe detector counts what you mark. "
        "It does not score, rank or diagnose anyone.\n\ngithub.com/korczis/prismatic-flipperzero",
        PRISMATIC_VERSION);
    show_text(app);
}

static void render_text(App* app) {
    Frame* f = top(app);
    PkpObject o;
    furi_string_reset(app->text);
    if(f->text == TxtInfo) {
        render_info(app);
        return;
    }
    if(f->text == TxtAbout) {
        render_about(app);
        return;
    }
    if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return;
    switch(f->text) {
    case TxtDefinition: {
        char kind[24];
        pkp_kind_name(&app->pack, o.kind, kind, sizeof(kind));
        furi_string_cat_printf(app->text, "%s\n", str(app, o.title));
        furi_string_cat_printf(app->text, "[%s", kind);
        if(o.code.len) furi_string_cat_printf(app->text, " %s", str(app, o.code));
        furi_string_cat_printf(app->text, "]\n\n%s\n", str(app, o.definition));
        break;
    }
    case TxtApply:
        if(count_items(app, &o, PKP_ITEM_ACTION)) {
            furi_string_cat_str(app->text, "WHAT TO DO\n");
            cat_items(app, &o, PKP_ITEM_ACTION, "- ");
        }
        if(count_items(app, &o, PKP_ITEM_ESCALATION)) {
            furi_string_cat_str(app->text, "\nWHEN TO ESCALATE\n");
            cat_items(app, &o, PKP_ITEM_ESCALATION, "- ");
        }
        break;
    case TxtCompare:
        furi_string_cat_str(app->text, "DON'T CONFUSE WITH\n");
        cat_items(app, &o, PKP_ITEM_ALTERNATIVE, "- ");
        break;
    case TxtValidation:
        furi_string_cat_str(app->text, "VALIDATION TESTS\n");
        cat_items(app, &o, PKP_ITEM_VALIDATION, "- ");
        break;
    case TxtChapter: {
        PkpItem it;
        if(pkp_item(&app->pack, f->aux, &it) != PKP_OK) break;
        furi_string_cat_printf(app->text, "%s\n\n", str(app, it.text));
        for(uint32_t k = f->aux + 1; k < o.items_first + o.items_count; k++) {
            if(pkp_item(&app->pack, k, &it) != PKP_OK || it.type != PKP_ITEM_TEXT) break;
            furi_string_cat_printf(app->text, "%s ", str(app, it.text));
        }
        break;
    }
    default:
        break;
    }
    show_text(app);
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
    case ScrObject:
        render_object(app);
        break;
    case ScrChapters:
        render_chapters(app);
        break;
    case ScrRelated:
        render_related(app);
        break;
    case ScrDetect:
        render_detect_question(app);
        break;
    case ScrQuiz:
        render_quiz(app);
        break;
    case ScrQuizOptions:
        render_quiz_options(app);
        break;
    default:
        render_text(app);
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
    Frame f = {.screen = ScrDetect, .obj = obj};
    push(app, f);
}

static bool on_custom(void* ctx, uint32_t event) {
    App* app = ctx;
    uint32_t action = event >> 16, arg = event & 0xFFFFu;
    Frame* f = top(app);
    Frame next = *f;
    switch(action) {
    case EvHome:
        if(arg == HomeBrowse) next = (Frame){.screen = ScrCategories};
        else if(arg == HomeInfo) next = (Frame){.screen = ScrInfo, .text = TxtInfo};
        else if(arg == HomeAbout) next = (Frame){.screen = ScrText, .text = TxtAbout};
        else {
            int32_t q = quiz_random(
                app, arg == HomeTrainDefinition ? PKP_QUIZ_DEFINITION : PKP_QUIZ_TRAINER);
            if(q < 0) return true;
            next = (Frame){.screen = ScrQuiz, .aux = (uint32_t)q, .text = (uint8_t)arg};
        }
        push(app, next);
        break;
    case EvCategory:
        f->cat = (uint16_t)arg; /* remembered for the selection when coming back */
        push(app, (Frame){.screen = ScrObjects, .cat = (uint16_t)arg});
        break;
    case EvPagePrev:
    case EvPageNext:
        f->page = (uint16_t)(f->page + (action == EvPageNext ? 1 : -1));
        render(app);
        break;
    case EvObject:
    case EvRelated:
        push(app, (Frame){.screen = ScrObject, .obj = (uint16_t)arg, .cat = f->cat});
        break;
    case EvObjectAction:
        if(arg == ObjDetect) {
            start_detect(app, f->obj);
            break;
        }
        if(arg == ObjQuiz) {
            int32_t q = quiz_for(app, f->obj, PKP_QUIZ_DEFINITION);
            if(q < 0) q = quiz_for(app, f->obj, PKP_QUIZ_TRAINER);
            if(q < 0) return true;
            push(app, (Frame){.screen = ScrQuiz, .obj = f->obj, .aux = (uint32_t)q});
            break;
        }
        next.screen = arg == ObjChapters ? ScrChapters : arg == ObjRelated ? ScrRelated : ScrText;
        next.text = arg == ObjDefinition ? TxtDefinition :
                    arg == ObjApply      ? TxtApply :
                    arg == ObjCompare    ? TxtCompare :
                                           TxtValidation;
        push(app, next);
        break;
    case EvChapter:
        next.screen = ScrText;
        next.text = TxtChapter;
        {
            PkpObject o; /* the event carries the item's offset inside its object */
            if(pkp_object(&app->pack, f->obj, &o) != PKP_OK) return true;
            next.aux = o.items_first + arg;
        }
        push(app, next);
        break;
    case EvMark: {
        app->marks[app->marker_at] = (uint8_t)arg;
        if(++app->marker_at < app->marker_count) {
            render_detect_question(app);
        } else {
            PkpObject o;
            if(pkp_object(&app->pack, f->obj, &o) == PKP_OK) {
                f->screen = ScrText; /* Back from the result returns to the object */
                f->text = TxtDetectResult;
                detect_result(app, &o);
            }
        }
        break;
    }
    case EvQuizNext:
        push(app, (Frame){.screen = ScrQuizOptions, .obj = f->obj, .aux = f->aux});
        break;
    case EvQuizOption:
        if(f->screen == ScrQuizOptions) {
            app->depth--; /* the result replaces the quiz and its options: Back leaves both */
            f = top(app);
        }
        f->screen = ScrText;
        f->text = TxtQuizResult;
        quiz_result(app, (uint8_t)arg);
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
    app->text_box = text_box_alloc();
    app->widget = widget_alloc();
    app->loading = loading_alloc();
    app->vd = view_dispatcher_alloc();
    view_dispatcher_add_view(app->vd, ViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->vd, ViewText, text_box_get_view(app->text_box));
    view_dispatcher_add_view(app->vd, ViewWidget, widget_get_view(app->widget));
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
    view_dispatcher_remove_view(app->vd, ViewText);
    view_dispatcher_remove_view(app->vd, ViewWidget);
    view_dispatcher_remove_view(app->vd, ViewLoading);
    view_dispatcher_free(app->vd);
    loading_free(app->loading);
    widget_free(app->widget);
    text_box_free(app->text_box);
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
