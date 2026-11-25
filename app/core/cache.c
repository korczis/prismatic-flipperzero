#include "cache.h"

#include <string.h>

void pkp_cache_init(PkpCache* c, PkpReadFn read, void* ctx, uint32_t size) {
    memset(c, 0, sizeof(*c));
    c->read = read;
    c->ctx = ctx;
    c->size = size;
}

void pkp_cache_clear(PkpCache* c) {
    memset(c->tag, 0, sizeof(c->tag));
}

/* The slot holding the block at `start`, loading it into the least recently used slot on a
 * miss; -1 when the underlying read fails (nothing is cached then). */
static int slot_for(PkpCache* c, uint32_t start) {
    int victim = 0;
    for(int i = 0; i < (int)PKP_CACHE_BLOCKS; i++) {
        if(c->tag[i] == start + 1) {
            c->hits++;
            c->used[i] = ++c->clock;
            return i;
        }
        /* an empty slot first, else the least recently used one */
        bool empty = c->tag[i] == 0, victim_empty = c->tag[victim] == 0;
        if((empty && !victim_empty) || (empty == victim_empty && c->used[i] < c->used[victim]))
            victim = i;
    }
    c->misses++;
    uint32_t n = c->size - start < PKP_CACHE_BLOCK ? c->size - start : PKP_CACHE_BLOCK;
    if(!c->read(c->ctx, start, c->data[victim], n)) {
        c->tag[victim] = 0;
        return -1;
    }
    c->tag[victim] = start + 1;
    c->len[victim] = n;
    c->used[victim] = ++c->clock;
    return victim;
}

bool pkp_cache_read(void* cache, uint32_t offset, void* buf, uint32_t len) {
    PkpCache* c = cache;
    uint8_t* out = buf;
    if(offset > c->size || len > c->size - offset) return false;
    while(len) {
        uint32_t start = offset - offset % PKP_CACHE_BLOCK;
        int s = slot_for(c, start);
        if(s < 0) return false;
        uint32_t at = offset - start, n = c->len[s] - at;
        if(n > len) n = len;
        memcpy(out, c->data[s] + at, n);
        out += n;
        offset += n;
        len -= n;
    }
    return true;
}
