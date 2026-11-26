#include "search.h"

#include <string.h>

#define CHUNK 32u /* postings per read: 64 bytes on the stack */

char pkp_search_letter(uint32_t position) {
    return position < PKP_SEARCH_LETTER_COUNT ? PKP_SEARCH_LETTERS[position] : '\0';
}

int pkp_search_position(char letter) {
    if(letter >= 'a' && letter <= 'z') return letter - 'a';
    if(letter >= '0' && letter <= '9') return 26 + (letter - '0');
    return -1;
}

/* The first key in [lo, hi) whose first n characters are not below q's (NUL past a key's end
 * sorts first, as the bytes of the table do). */
static PkpStatus
    lower(const PkpSearch* s, uint32_t lo, uint32_t hi, const char* q, uint8_t n, uint32_t* at) {
    while(lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        PkpSearchKey k;
        PkpStatus st = pkp_search_key(s->r, mid, &k);
        if(st != PKP_OK) return st;
        int cmp = 0;
        for(uint8_t i = 0; i < n && cmp == 0; i++)
            cmp = (int)(unsigned char)k.key[i] - (int)(unsigned char)q[i];
        if(cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    *at = lo;
    return PKP_OK;
}

/* The letters that follow the query's first `n` characters in its range: from each key found,
 * one lower bound jumps past every key that continues with the same letter. */
static PkpStatus letters(PkpSearch* s, uint8_t n) {
    uint64_t mask = 0;
    char q[PKP_SEARCH_KEY_LEN + 1];
    memcpy(q, s->query, n);
    for(uint32_t at = s->lo[n], hi = s->hi[n]; n < PKP_SEARCH_KEY_LEN && at < hi;) {
        PkpSearchKey k;
        PkpStatus st = pkp_search_key(s->r, at, &k);
        if(st != PKP_OK) return st;
        if(k.len <= n) { /* the key that is the query itself */
            at++;
            continue;
        }
        mask |= 1ull << pkp_search_position(k.key[n]);
        if(k.key[n] == 'z') break; /* the last character there is */
        q[n] = (char)(k.key[n] + 1);
        st = lower(s, at + 1, hi, q, (uint8_t)(n + 1), &at);
        if(st != PKP_OK) return st;
    }
    s->next[n] = mask;
    return PKP_OK;
}

/* The matches of keys [lo, hi): their postings are one run, streamed into the bitsets. */
static PkpStatus collect(PkpSearch* s, uint32_t lo, uint32_t hi) {
    PkpSearchKey a, b;
    PkpStatus st = pkp_search_key(s->r, lo, &a);
    if(st == PKP_OK) st = pkp_search_key(s->r, hi - 1, &b);
    if(st != PKP_OK) return st;
    uint32_t from = a.first, to = b.first + b.count;
    if(to < from) return PKP_E_SECTION; /* runs out of order: pkp_verify_search says so too */
    memset(s->any, 0, sizeof(s->any));
    memset(s->strong_bits, 0, sizeof(s->strong_bits));
    s->count = s->strong = 0;
    uint32_t objects = pkp_count(s->r, PKP_SEC_OBJECTS);
    const PkpSection* ps = &s->r->sec[PKP_SEC_SEARCH_POSTINGS];
    uint8_t chunk[CHUNK * 2];
    for(uint32_t at = from; at < to;) {
        uint32_t n = to - at < CHUNK ? to - at : CHUNK;
        if(!s->r->read(s->r->ctx, ps->offset + at * 2, chunk, n * 2)) return PKP_E_IO;
        for(uint32_t i = 0; i < n; i++) {
            uint16_t v = (uint16_t)(chunk[2 * i] | (chunk[2 * i + 1] << 8));
            uint16_t obj = (uint16_t)(v & ~PKP_SEARCH_STRONG);
            if(obj >= objects) return PKP_E_RANGE;
            uint8_t bit = (uint8_t)(1u << (obj & 7));
            if(!(s->any[obj >> 3] & bit)) s->count++;
            s->any[obj >> 3] |= bit;
            if((v & PKP_SEARCH_STRONG) && !(s->strong_bits[obj >> 3] & bit)) {
                s->strong_bits[obj >> 3] |= bit;
                s->strong++;
            }
        }
        at += n;
    }
    return PKP_OK;
}

void pkp_search_clear(PkpSearch* s) {
    s->len = 0;
    s->query[0] = '\0';
    s->count = s->strong = 0;
    memset(s->any, 0, sizeof(s->any));
    memset(s->strong_bits, 0, sizeof(s->strong_bits));
}

PkpStatus pkp_search_init(PkpSearch* s, const PkpReader* r) {
    memset(s, 0, sizeof(*s));
    s->r = r;
    uint32_t keys = pkp_count(r, PKP_SEC_SEARCH_KEYS);
    if(!pkp_has_search(r) || keys == 0) return PKP_E_MISSING;
    if(pkp_count(r, PKP_SEC_OBJECTS) > PKP_SEARCH_MAX_OBJECTS || keys > 0xFFFFu) {
        return PKP_E_RANGE;
    }
    s->keys = (uint16_t)keys;
    s->hi[0] = s->keys;
    return letters(s, 0);
}

uint64_t pkp_search_next(const PkpSearch* s) {
    return s->next[s->len];
}

/* An error mid-way leaves no half-made state: the query is emptied. */
static PkpStatus failed(PkpSearch* s, PkpStatus st) {
    pkp_search_clear(s);
    return st;
}

PkpStatus pkp_search_push(PkpSearch* s, char letter) {
    uint8_t n = s->len;
    int pos = pkp_search_position(letter);
    if(s->keys == 0 || n >= PKP_SEARCH_KEY_LEN || pos < 0 || !(s->next[n] & (1ull << pos))) {
        return PKP_E_RANGE;
    }
    char q[PKP_SEARCH_KEY_LEN + 1];
    memcpy(q, s->query, n);
    q[n] = letter;
    uint32_t lo, hi;
    PkpStatus st = lower(s, s->lo[n], s->hi[n], q, (uint8_t)(n + 1), &lo);
    q[n] = (char)(letter + 1);
    if(st == PKP_OK) st = lower(s, lo, s->hi[n], q, (uint8_t)(n + 1), &hi);
    if(st == PKP_OK && lo >= hi) st = PKP_E_RANGE; /* the mask and the table disagree */
    if(st == PKP_OK) st = collect(s, lo, hi);
    if(st != PKP_OK) return failed(s, st);
    s->query[n] = letter;
    s->query[n + 1] = '\0';
    s->len = (uint8_t)(n + 1);
    s->lo[n + 1] = (uint16_t)lo;
    s->hi[n + 1] = (uint16_t)hi;
    st = letters(s, (uint8_t)(n + 1));
    return st == PKP_OK ? PKP_OK : failed(s, st);
}

PkpStatus pkp_search_pop(PkpSearch* s) {
    if(s->len <= 1) {
        pkp_search_clear(s);
        return PKP_OK;
    }
    s->len--;
    s->query[s->len] = '\0';
    PkpStatus st = collect(s, s->lo[s->len], s->hi[s->len]);
    return st == PKP_OK ? PKP_OK : failed(s, st);
}

uint32_t pkp_search_results(const PkpSearch* s, uint32_t from, uint16_t* out, uint32_t cap) {
    uint32_t seen = 0, n = 0, objects = s->keys ? pkp_count(s->r, PKP_SEC_OBJECTS) : 0;
    for(int pass = 0; pass < 2; pass++) { /* strong first, then the rest */
        for(uint32_t obj = 0; obj < objects && n < cap; obj++) {
            uint8_t bit = (uint8_t)(1u << (obj & 7));
            bool strong = (s->strong_bits[obj >> 3] & bit) != 0;
            if(!(s->any[obj >> 3] & bit) || strong != (pass == 0)) continue;
            if(seen++ >= from) out[n++] = (uint16_t)obj;
        }
    }
    return n;
}
