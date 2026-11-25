/* Prismatic Pocket on the host: the portable app (app/ui) over a pack file, driven by a key
 * script, drawing with the firmware's own u8g2 and fonts.
 *
 *   pocketsim <pack.pkp|-> <out-dir> <keys>
 *
 * keys: comma-separated up, down, left, right, ok, back, each optionally ":long" or ":repeat".
 * Writes <out-dir>/NN-<key>.bin (1024-byte frames in the display's page layout, the format the
 * device streams over RPC; frame 00 is the start) and prints each frame's visible text, so
 * tests can assert on content and tools/sim.py can turn frames into PNGs. A pack path of "-"
 * starts with no pack; "!<path>" opens a pack whose open fails, to show the error path.
 * The steps "eject" and "insert" make every later pack read fail or work again: an SD card
 * pulled while the app runs. "tickN" passes N idle ticks of 250 ms (the label marquee).
 */
#define _POSIX_C_SOURCE 200809L /* strdup, strtok on glibc under -std=c11 */
#include "../../app/ui/ui_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE* f;
} PackFile;

static bool ejected;
static unsigned long file_reads, file_bytes;

/* The card: every read the app's cache could not serve lands here and is counted. */
static bool read_cb(void* ctx, uint32_t offset, void* buf, uint32_t len) {
    PackFile* pf = ctx;
    if(ejected) return false;
    file_reads++;
    file_bytes += len;
    return fseek(pf->f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, pf->f) == len;
}

/* Quiz progress in the run's folder: progress.dat and its temporary copy, written as the
 * device does (the copy, then a rename). "full" makes writes fail like a full card. */
static char progress_dir[400];
static bool card_full, no_rename, tear;

static size_t progress_read(void* ctx, int copy, uint8_t* buf, size_t cap) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/progress.%s", progress_dir, copy ? "tmp" : "dat");
    FILE* f = fopen(path, "rb");
    if(!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool progress_write(void* ctx, const uint8_t* data, size_t len) {
    (void)ctx;
    char tmp[512], dst[512];
    snprintf(tmp, sizeof(tmp), "%s/progress.tmp", progress_dir);
    snprintf(dst, sizeof(dst), "%s/progress.dat", progress_dir);
    if(card_full) return false;
    FILE* f = fopen(tmp, "wb");
    if(tear) { /* power lost mid-write: half the copy, no rename */
        tear = false;
        if(f) {
            fwrite(data, 1, len / 2, f);
            fclose(f);
        }
        return false;
    }
    bool ok = f && fwrite(data, 1, len, f) == len;
    if(f) ok = fclose(f) == 0 && ok;
    return ok && (no_rename || rename(tmp, dst) == 0);
}

static void progress_forget(void* ctx) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/progress.dat", progress_dir);
    remove(path);
    snprintf(path, sizeof(path), "%s/progress.tmp", progress_dir);
    remove(path);
}

/* Saved sessions in the run's folder, as JSON Lines; "full" refuses them too. */
static bool session_append(void* ctx, const char* line, size_t len) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions.jsonl", progress_dir);
    FILE* f = fopen(path, "ab");
    if(!f) return false;
    long size = ftell(f);
    bool ok = !card_full && size >= 0 && (size_t)size + len <= SESSION_MAX_FILE &&
              fwrite(line, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static size_t session_read(void* ctx, uint32_t offset, char* buf, size_t cap) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions.jsonl", progress_dir);
    FILE* f = fopen(path, "rb");
    if(!f) return 0;
    size_t n = fseek(f, (long)offset, SEEK_SET) == 0 ? fread(buf, 1, cap, f) : 0;
    fclose(f);
    return n;
}

static void session_forget(void* ctx) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/sessions.jsonl", progress_dir);
    remove(path);
}

static void now(void* ctx, char* buf, size_t cap) {
    (void)ctx;
    snprintf(buf, cap, "2026-11-25T12:00:00"); /* a fixed clock: runs are reproducible */
}

static uint32_t seed = 12345;
static uint32_t rnd(void* ctx) {
    (void)ctx;
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static uint32_t clock_ms;
static uint32_t tick(void* ctx) {
    (void)ctx;
    return clock_ms += 7;
}

static void heap(void* ctx, uint32_t* free_b, uint32_t* largest) {
    (void)ctx;
    *free_b = 123456;
    *largest = 65432;
}

static void frame(UiApp* app, Canvas* canvas, const char* dir, int n, const char* name) {
    char path[512], lines[16][64];
    ui_app_draw(app, canvas);
    snprintf(path, sizeof(path), "%s/%02d-%s.bin", dir, n, name);
    FILE* out = fopen(path, "wb");
    if(!out || fwrite(sim_canvas_frame(canvas), 1, 1024, out) != 1024) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fclose(out);
    uint16_t k = page_visible_lines(ui_app_page(app), lines, 16);
    printf("== %02d %s\n@ %s\n", n, name, ui_app_screen_name(app));
    for(uint16_t i = 0; i < k; i++)
        printf("%s\n", lines[i]);
}

static bool parse(const char* tok, PageKey* key, PagePress* press) {
    static const char* const names[] = {"up", "down", "left", "right", "ok", "back"};
    char name[16];
    const char* colon = strchr(tok, ':');
    size_t n = colon ? (size_t)(colon - tok) : strlen(tok);
    if(n == 0 || n >= sizeof(name)) return false;
    memcpy(name, tok, n);
    name[n] = '\0';
    *press = PagePressShort;
    if(colon && strcmp(colon + 1, "long") == 0) *press = PagePressLong;
    if(colon && strcmp(colon + 1, "repeat") == 0) *press = PagePressRepeat;
    for(int i = 0; i < 6; i++) {
        if(strcmp(name, names[i]) == 0) {
            *key = (PageKey)i;
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    if(argc != 4) {
        fprintf(stderr, "usage: pocketsim <pack.pkp|-|!pack> <out-dir> <keys>\n");
        return 2;
    }
    /* POCKET_NOCACHE=1 reads the card directly, to measure what the cache saves */
    static PkpCache cache;
    bool nocache = getenv("POCKET_NOCACHE") != NULL;
    UiPlatform platform = {
        rnd,
        tick,
        heap,
        NULL,
        &cache,
        progress_read,
        progress_write,
        progress_forget,
        session_append,
        session_read,
        session_forget,
        now};
    snprintf(progress_dir, sizeof(progress_dir), "%s", argv[2]);
    UiApp* app = ui_app_alloc(&platform);
    Canvas* canvas = sim_canvas_alloc();
    ui_app_loading(app);
    frame(app, canvas, argv[2], 0, "loading");

    PackFile pf = {NULL};
    PkpReader reader;
    PkpStatus status = PKP_E_MISSING;
    const char* path = argv[1];
    bool broken = path[0] == '!';
    if(broken) path++;
    if(strcmp(path, "-") != 0) {
        pf.f = fopen(path, "rb");
        status = PKP_E_IO;
        if(pf.f) {
            fseek(pf.f, 0, SEEK_END);
            uint32_t size = (uint32_t)ftell(pf.f);
            if(broken) size--;
            pkp_cache_init(&cache, read_cb, &pf, size);
            status = nocache ? pkp_open(&reader, read_cb, &pf, size) :
                               pkp_open(&reader, pkp_cache_read, &cache, size);
        }
    }
    ui_app_set_pack(app, status == PKP_OK ? &reader : NULL, status, 42);
    frame(app, canvas, argv[2], 0, "start");

    char* keys = strdup(argv[3]);
    int n = 1, rc = 0;
    for(char* tok = strtok(keys, ","); tok; tok = strtok(NULL, ",")) {
        PageKey key;
        PagePress press;
        if(strncmp(tok, "tick", 4) == 0) {
            /* "tick" or "tickN": N idle ticks of 250 ms, as the device's input loop gives them */
            int ticks = tok[4] ? atoi(tok + 4) : 1;
            bool redraw = false;
            for(int t = 0; t < ticks; t++)
                redraw = ui_app_tick(app) || redraw;
            char name[24];
            snprintf(name, sizeof(name), "%s%s", tok, redraw ? "" : "-idle");
            frame(app, canvas, argv[2], n++, name);
            continue;
        }
        if(strcmp(tok, "restart") == 0 || strcmp(tok, "full") == 0 || strcmp(tok, "tear") == 0 ||
           strcmp(tok, "norename") == 0 || strcmp(tok, "rekey") == 0 ||
           strcmp(tok, "tornsession") == 0 || strcmp(tok, "foreign") == 0) {
            /* restart: quit and start again over the same files; full: writes fail from now;
             * tear: the next save stops halfway through the copy (power lost); norename: later
             * saves leave the temporary copy and never rename it (power lost in between);
             * rekey: the first entry's key stops matching its question; tornsession and foreign
             * write session lines (below) */
            if(strcmp(tok, "full") == 0) card_full = true;
            if(tok[0] == 't') tear = true;
            if(tok[0] == 'n') no_rename = true;
            /* tornsession: a session line cut off mid-write; foreign: a whole session about an
             * object this pack does not have */
            if(strcmp(tok, "tornsession") == 0) session_append(NULL, "{\"v\":1,\"t\":\"20", 14);
            if(strcmp(tok, "foreign") == 0) {
                static const char line[] =
                    "{\"v\":1,\"t\":\"2026-01-02T03:04:05\",\"pack\":\"x\",\"c\":[{\"id\":\"elsewhere/x\","
                    "\"i\":0,\"m\":\"O\"}]}\n";
                session_append(NULL, line, sizeof(line) - 1);
            }
            if(tok[1] == 'e' && tok[2] == 'k') {
                /* rekey: the first answered question's key no longer matches its prompt, as
                 * after a pack rebuild that changed the question's text */
                static Progress pr;
                static uint8_t buf[PROGRESS_MAX_FILE];
                size_t len = progress_read(NULL, 0, buf, sizeof(buf));
                if(progress_decode(&pr, buf, len) == PROGRESS_OK && pr.count) {
                    pr.entries[0].key ^= 1u;
                    progress_write(NULL, buf, progress_encode(&pr, buf, sizeof(buf)));
                }
            }
            if(tok[0] == 'r') {
                ui_app_free(app);
                app = ui_app_alloc(&platform);
                ui_app_set_pack(app, status == PKP_OK ? &reader : NULL, status, 42);
                frame(app, canvas, argv[2], n++, tok); /* drawn, so keys act on it */
                continue;
            }
            printf("== %s\n", tok);
            continue;
        }
        if(strcmp(tok, "eject") == 0 || strcmp(tok, "insert") == 0) {
            /* the cache is cleared too, so every later read reaches the missing card */
            ejected = tok[0] == 'e';
            pkp_cache_clear(&cache);
            printf("== %s\n", tok);
            continue;
        }
        if(!parse(tok, &key, &press)) {
            fprintf(stderr, "unknown key: %s\n", tok);
            rc = 2;
            break;
        }
        bool running = ui_app_input(app, key, press);
        char name[24];
        snprintf(name, sizeof(name), "%s", tok);
        for(char* c = name; *c; c++)
            if(*c == ':') *c = '-';
        if(!running) {
            printf("== %02d %s\nEXIT\n", n, name);
            break;
        }
        frame(app, canvas, argv[2], n++, name);
    }
    free(keys);
    fprintf(
        stderr,
        "reads: %lu card reads, %lu bytes; cache %lu hits, %lu misses\n",
        file_reads,
        file_bytes,
        (unsigned long)cache.hits,
        (unsigned long)cache.misses);
    if(pf.f) fclose(pf.f);
    sim_canvas_free(canvas);
    ui_app_free(app);
    return rc;
}
