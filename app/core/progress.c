#include "progress.h"

#include <string.h>

#include "pkp.h"

#define VERSION 1u

static void put16(uint8_t* b, uint16_t v) {
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* b, uint32_t v) {
    put16(b, (uint16_t)v);
    put16(b + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t* b) {
    return (uint16_t)(b[0] | b[1] << 8);
}

static uint32_t get32(const uint8_t* b) {
    return get16(b) | (uint32_t)get16(b + 2) << 16;
}

uint32_t progress_key(const char* text) {
    uint32_t h = 2166136261u;
    for(; *text; text++)
        h = (h ^ (uint8_t)*text) * 16777619u;
    return h;
}

void progress_init(Progress* p) {
    memset(p, 0, sizeof(*p));
}

ProgressStatus progress_decode(Progress* p, const uint8_t* d, size_t len) {
    progress_init(p);
    if(len < PROGRESS_HEADER + 4) return PROGRESS_E_SHORT;
    if(memcmp(d, "PKPS", 4) != 0) return PROGRESS_E_MAGIC;
    if(get16(d + 4) != VERSION) return PROGRESS_E_VERSION;
    uint16_t count = get16(d + 14);
    if(count > PROGRESS_CAP) return PROGRESS_E_COUNT;
    size_t body = PROGRESS_HEADER + (size_t)count * PROGRESS_ENTRY;
    if(len < body + 4) return PROGRESS_E_SHORT;
    if(pkp_crc32(0, d, body) != get32(d + body)) return PROGRESS_E_CRC;
    p->generation = get32(d + 8);
    p->session = get16(d + 12);
    p->count = count;
    for(uint16_t i = 0; i < count; i++) {
        const uint8_t* e = d + PROGRESS_HEADER + (size_t)i * PROGRESS_ENTRY;
        ProgressEntry* x = &p->entries[i];
        x->key = get32(e);
        x->quiz = get16(e + 4);
        x->box = e[6] >= 1 && e[6] <= PROGRESS_BOXES ? e[6] : 1;
        x->last_right = e[7] ? 1 : 0;
        x->attempts = get16(e + 8);
        x->due = get16(e + 10);
    }
    return PROGRESS_OK;
}

int progress_load(Progress* p, const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
    static Progress other; /* no allocation; the app holds one progress at a time */
    bool va = a && progress_decode(p, a, alen) == PROGRESS_OK;
    bool vb = b && progress_decode(&other, b, blen) == PROGRESS_OK;
    if(vb && (!va || other.generation > p->generation)) {
        *p = other;
        return 2;
    }
    if(va) return 1;
    progress_init(p);
    return 0;
}

size_t progress_encode(Progress* p, uint8_t* out, size_t cap) {
    size_t body = PROGRESS_HEADER + (size_t)p->count * PROGRESS_ENTRY;
    if(cap < body + 4) return 0;
    p->generation++;
    memcpy(out, "PKPS", 4);
    put16(out + 4, VERSION);
    put16(out + 6, 0);
    put32(out + 8, p->generation);
    put16(out + 12, p->session);
    put16(out + 14, p->count);
    put32(out + 16, 0);
    for(uint16_t i = 0; i < p->count; i++) {
        uint8_t* e = out + PROGRESS_HEADER + (size_t)i * PROGRESS_ENTRY;
        const ProgressEntry* x = &p->entries[i];
        put32(e, x->key);
        put16(e + 4, x->quiz);
        e[6] = x->box;
        e[7] = x->last_right;
        put16(e + 8, x->attempts);
        put16(e + 10, x->due);
    }
    put32(out + body, pkp_crc32(0, out, body));
    return body + 4;
}

void progress_record(Progress* p, uint32_t key, uint16_t quiz, bool right) {
    int at = -1;
    for(int i = 0; i < p->count; i++)
        if(p->entries[i].key == key) at = i;
    if(at < 0) {
        if(p->count == PROGRESS_CAP) {
            /* full: the entry due furthest in the future is the one least needed */
            at = 0;
            for(int i = 1; i < p->count; i++)
                if(p->entries[i].due > p->entries[at].due) at = i;
        } else {
            at = p->count++;
        }
        p->entries[at] = (ProgressEntry){.key = key, .box = 1};
    }
    ProgressEntry* e = &p->entries[at];
    e->quiz = quiz;
    e->attempts++;
    e->last_right = right;
    if(!right)
        e->box = 1;
    else if(e->box < PROGRESS_BOXES)
        e->box++;
    e->due = (uint16_t)(p->session + (1u << (e->box - 1)));
}

uint16_t progress_due_count(const Progress* p) {
    uint16_t n = 0;
    for(int i = 0; i < p->count; i++)
        n += p->entries[i].due <= p->session;
    return n;
}

int progress_next_due(const Progress* p) {
    int best = -1;
    for(int i = 0; i < p->count; i++) {
        const ProgressEntry* e = &p->entries[i];
        if(e->due > p->session) continue;
        if(best < 0 || e->box < p->entries[best].box ||
           (e->box == p->entries[best].box && e->due < p->entries[best].due))
            best = i;
    }
    return best;
}

void progress_drop(Progress* p, int index) {
    p->entries[index] = p->entries[--p->count];
}
