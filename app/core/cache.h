/* A small LRU block cache in front of a pack's reads. Portable C99, no allocation.
 *
 * The pack reader asks for many small records (40-byte objects, 8-byte items, strings) that
 * cluster: an article reads one object, its items in a row, and their strings. Eight 256-byte
 * blocks (2 KiB) keep those clusters in memory; a request is served block by block, so any
 * length works. Hits and misses are counted so the app can show how well the cache does.
 */
#pragma once

#include "pkp.h"

#define PKP_CACHE_BLOCKS 8u
#define PKP_CACHE_BLOCK  256u

typedef struct {
    PkpReadFn read;
    void* ctx;
    uint32_t size;
    uint32_t clock;
    uint32_t hits;
    uint32_t misses;
    uint32_t tag[PKP_CACHE_BLOCKS]; /* block start offset + 1; 0 = empty */
    uint32_t used[PKP_CACHE_BLOCKS]; /* clock of the last use */
    uint32_t len[PKP_CACHE_BLOCKS];
    uint8_t data[PKP_CACHE_BLOCKS][PKP_CACHE_BLOCK];
} PkpCache;

/* `read` reads the underlying file of `size` bytes. */
void pkp_cache_init(PkpCache* cache, PkpReadFn read, void* ctx, uint32_t size);
/* A PkpReadFn: pass pkp_cache_read with the cache as its context. */
bool pkp_cache_read(void* cache, uint32_t offset, void* buf, uint32_t len);
/* Forget every block (after the file changed or a read error the caller wants retried). */
void pkp_cache_clear(PkpCache* cache);
