/* Prismatic Pocket's screens and navigation, portable (no Furi): the app is a function of the
 * pack and the key presses. app/prismatic.c feeds it the device's pack file, keys and clock;
 * tests/sim feeds it a pack file and a key script and gets the same frames.
 *
 * No screen knows any concept: categories, objects, articles, the actions an article links to
 * (detector, quiz, chapters, related and confusable objects) and every text come from the
 * pack. A new object with existing capabilities needs no change here. The detector only counts
 * the operator's marks; it never scores or diagnoses.
 */
#pragma once

#include "../core/cache.h"
#include "../core/pkp.h"
#include "../core/progress.h"
#include "../core/session.h"
#include "page.h"

typedef struct {
    uint32_t (*random)(void* ctx);
    uint32_t (*tick_ms)(void* ctx);
    void (*heap)(void* ctx, uint32_t* free_bytes, uint32_t* largest_block);
    void* ctx;
    const PkpCache* cache; /* the pack's block cache, shown on the Info screen */
    /* quiz progress on storage: read copy 0 (the file) or 1 (its temporary copy) into buf and
     * return its length (0: none); write replaces the file safely; forget deletes both */
    size_t (*progress_read)(void* ctx, int copy, uint8_t* buf, size_t cap);
    bool (*progress_write)(void* ctx, const uint8_t* data, size_t len);
    void (*progress_forget)(void* ctx);
    /* saved sessions (app/core/session.h): append one line (false when the card refuses it or
     * the file would pass SESSION_MAX_FILE), read bytes at an offset, delete the file */
    bool (*session_append)(void* ctx, const char* line, size_t len);
    size_t (*session_read)(void* ctx, uint32_t offset, char* buf, size_t cap);
    void (*session_forget)(void* ctx);
    /* the clock's local time as "YYYY-MM-DDTHH:MM:SS" */
    void (*now)(void* ctx, char* buf, size_t cap);
} UiPlatform;

typedef struct UiApp UiApp;

UiApp* ui_app_alloc(const UiPlatform* platform);
void ui_app_free(UiApp* app);
/* Shown while the pack opens. */
void ui_app_loading(UiApp* app);
/* The opened pack (or why there is none) and how long opening took; scans the index once. */
void ui_app_set_pack(UiApp* app, PkpReader* pack, PkpStatus status, uint32_t open_ms);
void ui_app_draw(UiApp* app, Canvas* canvas);
/* Returns false when the app should exit (Back on Home). */
bool ui_app_input(UiApp* app, PageKey key, PagePress press);
/* Idle time, about every 250 ms; returns true when the screen needs a redraw. */
bool ui_app_tick(UiApp* app);
Page* ui_app_page(UiApp* app);
/* The screen on top, by name ("Home", "Article", ...): for tests and the screen atlas. */
const char* ui_app_screen_name(const UiApp* app);
