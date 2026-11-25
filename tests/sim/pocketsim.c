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
 * pulled while the app runs.
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
    UiPlatform platform = {rnd, tick, heap, NULL, &cache};
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
