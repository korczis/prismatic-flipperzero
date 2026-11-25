/* Prismatic Pocket for Flipper Zero: the Furi glue around the portable app in app/ui.
 *
 * Everything the user sees is app/ui/ui_app.c drawing through app/ui/page.c; this file only
 * opens the pack on the SD card, forwards keys and draws. The same app runs on the host in
 * tests/sim with the firmware's own fonts, so every screen is tested and screenshotted there.
 */
#include <furi.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <storage/storage.h>

#include "core/cache.h"
#include "core/pkp.h"
#include "ui/ui_app.h"

#define TAG           "Prismatic"
#define PACK_DIR      EXT_PATH("apps_data/prismatic")
#define PROGRESS_FILE PACK_DIR "/progress.bin"
#define PROGRESS_TMP  PACK_DIR "/progress.tmp"

typedef struct {
    File* file;
} PackFile;

typedef struct {
    FuriMutex* mutex;
    FuriMessageQueue* input;
    ViewPort* view_port;
    UiApp* ui;
} App;

/* A read from the card. The app reads through a PkpCache (app/core/cache.c) in front of this,
 * so only blocks it has not kept reach the card. */
static bool pack_read(void* ctx, uint32_t offset, void* buf, uint32_t len) {
    PackFile* pf = ctx;
    if(!storage_file_seek(pf->file, offset, true)) return false;
    return storage_file_read(pf->file, buf, len) == len;
}

/* The first *.pkp in PACK_DIR by name, opened and checked; the status says why when not. */
static PkpStatus
    pack_open(Storage* storage, PackFile* pf, PkpCache* cache, PkpReader* reader, uint32_t* ms) {
    PkpStatus status = PKP_E_MISSING;
    File* dir = storage_file_alloc(storage);
    char name[128], best[128] = "";
    if(storage_dir_open(dir, PACK_DIR)) {
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            size_t n = strlen(name);
            if(!file_info_is_dir(&info) && n > 4 && strcmp(name + n - 4, ".pkp") == 0 &&
               (!best[0] || strcmp(name, best) < 0)) {
                strlcpy(best, name, sizeof(best));
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    if(!best[0]) return status;
    FuriString* path = furi_string_alloc_printf("%s/%s", PACK_DIR, best);
    pf->file = storage_file_alloc(storage);
    status = PKP_E_IO;
    if(storage_file_open(pf->file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t t0 = furi_get_tick();
        uint32_t size = (uint32_t)storage_file_size(pf->file);
        pkp_cache_init(cache, pack_read, pf, size);
        status = pkp_open(reader, pkp_cache_read, cache, size);
        *ms = furi_get_tick() - t0;
    }
    furi_string_free(path);
    return status;
}

/* Quiz progress (app/core/progress.h): the temporary copy is written and closed first, then
 * the file is replaced by it. The storage API's rename refuses an existing target, so the old
 * file is removed before the rename; if power fails between the two, the copy alone survives,
 * and the reader takes the valid copy with the higher generation. */
static size_t progress_read(void* ctx, int copy, uint8_t* buf, size_t cap) {
    Storage* storage = ctx;
    File* f = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(f, copy ? PROGRESS_TMP : PROGRESS_FILE, FSAM_READ, FSOM_OPEN_EXISTING))
        n = storage_file_read(f, buf, cap);
    storage_file_close(f);
    storage_file_free(f);
    return n;
}

static bool progress_write(void* ctx, const uint8_t* data, size_t len) {
    Storage* storage = ctx;
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(f, PROGRESS_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(f, data, len) == len;
    ok = storage_file_close(f) && ok;
    storage_file_free(f);
    if(!ok) return false;
    storage_common_remove(storage, PROGRESS_FILE);
    return storage_common_rename(storage, PROGRESS_TMP, PROGRESS_FILE) == FSE_OK;
}

static void progress_forget(void* ctx) {
    Storage* storage = ctx;
    storage_common_remove(storage, PROGRESS_FILE);
    storage_common_remove(storage, PROGRESS_TMP);
}

static uint32_t platform_random(void* ctx) {
    UNUSED(ctx);
    return furi_hal_random_get();
}

static uint32_t platform_tick(void* ctx) {
    UNUSED(ctx);
    return furi_get_tick();
}

static void platform_heap(void* ctx, uint32_t* free_bytes, uint32_t* largest) {
    UNUSED(ctx);
    *free_bytes = (uint32_t)memmgr_get_free_heap();
    *largest = (uint32_t)memmgr_heap_get_max_free_block();
}

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    ui_app_draw(app->ui, canvas);
    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->input, event, FuriWaitForever);
}

/* The loader's "close" (and anything else that asks the app to quit) arrives as a thread
 * signal; it wakes the input loop with a key no button sends. */
static bool signal_cb(uint32_t signal, void* arg, void* ctx) {
    UNUSED(arg);
    App* app = ctx;
    if(signal != FuriSignalExit) return false;
    InputEvent quit = {.key = InputKeyMAX, .type = InputTypeShort};
    furi_message_queue_put(app->input, &quit, FuriWaitForever);
    return true;
}

static bool to_page(const InputEvent* e, PageKey* key, PagePress* press) {
    static const PageKey keys[] = {
        [InputKeyUp] = PageKeyUp,
        [InputKeyDown] = PageKeyDown,
        [InputKeyRight] = PageKeyRight,
        [InputKeyLeft] = PageKeyLeft,
        [InputKeyOk] = PageKeyOk,
        [InputKeyBack] = PageKeyBack,
    };
    if(e->key >= COUNT_OF(keys)) return false;
    *key = keys[e->key];
    if(e->type == InputTypeShort)
        *press = PagePressShort;
    else if(e->type == InputTypeLong)
        *press = PagePressLong;
    else if(e->type == InputTypeRepeat)
        *press = PagePressRepeat;
    else
        return false;
    return true;
}

int32_t prismatic_app(void* p) {
    UNUSED(p);
    App app;
    PkpCache* cache = malloc(sizeof(PkpCache));
    pkp_cache_init(cache, NULL, NULL, 0);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    UiPlatform platform = {
        platform_random,
        platform_tick,
        platform_heap,
        storage,
        cache,
        progress_read,
        progress_write,
        progress_forget};
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app.ui = ui_app_alloc(&platform);
    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, draw_cb, &app);
    view_port_input_callback_set(app.view_port, input_cb, &app);
    furi_thread_set_signal_callback(furi_thread_get_current(), signal_cb, &app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app.view_port, GuiLayerFullscreen);

    ui_app_loading(app.ui);
    view_port_update(app.view_port);
    PackFile pf = {0};
    PkpReader reader;
    uint32_t open_ms = 0;
    PkpStatus status = pack_open(storage, &pf, cache, &reader, &open_ms);
    furi_mutex_acquire(app.mutex, FuriWaitForever);
    ui_app_set_pack(app.ui, status == PKP_OK ? &reader : NULL, status, open_ms);
    furi_mutex_release(app.mutex);
    FURI_LOG_I(TAG, "pack: %s, open %lu ms", pkp_status_text(status), (unsigned long)open_ms);
    view_port_update(app.view_port);

    InputEvent event;
    bool running = true;
    while(running) {
        PageKey key;
        PagePress press;
        if(furi_message_queue_get(app.input, &event, 250) != FuriStatusOk) {
            /* idle for 250 ms: let a cut label scroll */
            furi_mutex_acquire(app.mutex, FuriWaitForever);
            bool redraw = ui_app_tick(app.ui);
            furi_mutex_release(app.mutex);
            if(redraw) view_port_update(app.view_port);
            continue;
        }
        if(event.key == InputKeyMAX) break; /* the loader asked us to quit */
        if(!to_page(&event, &key, &press)) continue;
        furi_mutex_acquire(app.mutex, FuriWaitForever);
        running = ui_app_input(app.ui, key, press);
        furi_mutex_release(app.mutex);
        view_port_update(app.view_port);
    }

    furi_thread_set_signal_callback(furi_thread_get_current(), NULL, NULL);
    gui_remove_view_port(gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    if(pf.file) {
        storage_file_close(pf.file);
        storage_file_free(pf.file);
    }
    furi_record_close(RECORD_STORAGE);
    ui_app_free(app.ui);
    free(cache);
    furi_message_queue_free(app.input);
    furi_mutex_free(app.mutex);
    return 0;
}
