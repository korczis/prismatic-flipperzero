/* Host tests for the portable core (app/core). Built with warnings as errors, ASan and UBSan.
 *
 *   test_pkp test <pack>          unit, corruption and mutation-fuzz tests against a real pack
 *   test_pkp dump <pack>          canonical JSON dump (compared with tools/pocket.py decode)
 *   test_pkp walk <dir>           verdict "ok" or the status for every file in a list
 *                                 (<dir>/list.txt), for the differential fuzz test
 */
#include "../../app/core/cache.h"
#include "../../app/core/pkp.h"
#include "../../app/core/pocket.h"
#include "../../app/core/progress.h"
#include "../../app/core/session.h"
#include "../../app/core/bayes.h"
#include "../../app/core/scorecard.h"
#include "../../app/core/simulate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t* data;
    uint32_t size;
    int fail_after; /* -1: never; otherwise the read that fails */
} Mem;

static bool mem_read(void* ctx, uint32_t off, void* buf, uint32_t len) {
    Mem* m = ctx;
    if(m->fail_after == 0) return false;
    if(m->fail_after > 0) m->fail_after--;
    if((uint64_t)off + len > m->size) return false;
    memcpy(buf, m->data + off, len);
    return true;
}

static uint8_t* load(const char* path, uint32_t* size) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* d = malloc((size_t)n);
    if(!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "read %s failed\n", path);
        exit(2);
    }
    fclose(f);
    *size = (uint32_t)n;
    return d;
}

#define FUZZ_ROUNDS 2000

static int failures;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if(!(cond)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while(0)

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Recompute the CRC so a deeper check, not the checksum, has to catch the corruption. */
static uint32_t le32(const uint8_t* p);
static uint32_t dir_entry(const uint8_t* d, uint16_t type);
static void reseal(uint8_t* d, uint32_t size) {
    uint32_t e = dir_entry(d, PKP_SEC_STRINGS);
    uint32_t so = e ? le32(d + e + 4) : size, sl = e ? le32(d + e + 8) : 0;
    if((uint64_t)so + sl > size || so < PKP_HEADER_SIZE) { so = size; sl = 0; }
    uint32_t c = pkp_crc32(0, d + PKP_HEADER_SIZE, so - PKP_HEADER_SIZE);
    c = pkp_crc32(c, d + so + sl, size - so - sl);
    put32(d + 12, c);
}

/* Open and read every record; the first failing status, or PKP_OK. */
static PkpStatus walk(const uint8_t* d, uint32_t size) {
    Mem m = {d, size, -1};
    PkpReader r;
    PkpStatus st = pkp_open(&r, mem_read, &m, size);
    if(st != PKP_OK) return st;
    PkpStr s;
    for(uint32_t i = 0; i < PKP_META_COUNT; i++)
        if((st = pkp_meta(&r, i, &s)) != PKP_OK) return st;
    PkpCategory c;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_CATEGORIES); i++)
        if((st = pkp_category(&r, i, &c)) != PKP_OK) return st;
    PkpObject o;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_OBJECTS); i++)
        if((st = pkp_object(&r, i, &o)) != PKP_OK) return st;
    PkpItem it;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_ITEMS); i++)
        if((st = pkp_item(&r, i, &it)) != PKP_OK) return st;
    PkpRelation rel;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_RELATIONS); i++)
        if((st = pkp_relation(&r, i, &rel)) != PKP_OK) return st;
    PkpQuiz q;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_QUIZ); i++)
        if((st = pkp_quiz(&r, i, &q)) != PKP_OK) return st;
    char name[64];
    for(uint32_t i = 1; i <= pkp_count(&r, PKP_SEC_KINDS); i++)
        if((st = pkp_kind_name(&r, (uint8_t)i, name, sizeof(name))) != PKP_OK) return st;
    for(uint32_t i = 1; i <= pkp_count(&r, PKP_SEC_RELNAMES); i++)
        if((st = pkp_relation_name(&r, (uint8_t)i, name, sizeof(name))) != PKP_OK) return st;
    if(!pkp_has_search(&r)) return PKP_OK;
    PkpSearchKey k;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_SEARCH_KEYS); i++)
        if((st = pkp_search_key(&r, i, &k)) != PKP_OK) return st;
    uint16_t obj;
    bool strong;
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_SEARCH_POSTINGS); i++)
        if((st = pkp_posting(&r, i, &obj, &strong)) != PKP_OK) return st;
    return pkp_verify_search(&r);
}

/* Byte offset of section `type`'s directory entry, or 0. */
static uint32_t dir_entry(const uint8_t* d, uint16_t type) {
    uint16_t n = (uint16_t)(d[16] | (d[17] << 8));
    for(uint32_t k = 0; k < n; k++) {
        const uint8_t* e = d + PKP_HEADER_SIZE + k * PKP_SECTION_SIZE;
        if((uint16_t)(e[0] | (e[1] << 8)) == type) return PKP_HEADER_SIZE + k * PKP_SECTION_SIZE;
    }
    return 0;
}

static uint32_t sec_offset(const uint8_t* d, uint16_t type);
static uint32_t sec_offset(const uint8_t* d, uint16_t type) {
    return le32(d + dir_entry(d, type) + 4);
}

typedef void (*Mutate)(uint8_t* d, uint32_t size);

static PkpStatus mutated(const uint8_t* pack, uint32_t size, Mutate fn, int seal) {
    uint8_t* d = malloc(size);
    memcpy(d, pack, size);
    fn(d, size);
    if(seal) reseal(d, size);
    PkpStatus st = walk(d, size);
    free(d);
    return st;
}

static void m_magic(uint8_t* d, uint32_t n) { (void)n; d[0] = 'X'; }
static void m_version(uint8_t* d, uint32_t n) { (void)n; put16(d + 4, 2); }
static void m_features(uint8_t* d, uint32_t n) { (void)n; put16(d + 6, 1); }
static void m_size(uint8_t* d, uint32_t n) { put32(d + 8, n + 1); }
static void m_flip(uint8_t* d, uint32_t n) { d[n - 1] ^= 0x40; }
static void m_nsec(uint8_t* d, uint32_t n) { (void)n; put16(d + 16, 40); }
static void m_dir_past_end(uint8_t* d, uint32_t n) { put16(d + 16, (uint16_t)((n - 32) / 16 + 1)); }
static void m_sec_overflow(uint8_t* d, uint32_t n) { (void)n; put32(d + dir_entry(d, PKP_SEC_ITEMS) + 4, 0xFFFFFFF0u); }
static void m_entry_size(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_OBJECTS) + 2, 41); }
static void m_count(uint8_t* d, uint32_t n) { (void)n; uint32_t e = dir_entry(d, PKP_SEC_ITEMS); put32(d + e + 12, le32(d + e + 12) + 1); }
static void m_missing(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_QUIZ), 99); }
static void m_objcount(uint8_t* d, uint32_t n) { (void)n; put32(d + 20, le32(d + 20) + 1); }
static void m_obj_string(uint8_t* d, uint32_t n) { (void)n; put32(d + sec_offset(d, PKP_SEC_OBJECTS), 0xFFFFFF00u); }
static void m_obj_kind(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_OBJECTS) + 26] = 0; }
static void m_obj_items(uint8_t* d, uint32_t n) { (void)n; put32(d + sec_offset(d, PKP_SEC_OBJECTS) + 28, 0xFFFFFFFFu); }
static void m_obj_category(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_OBJECTS) + 24, 0xFFFF); }
static void m_item_type(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_ITEMS)] = PKP_ITEM_LAST + 1; }
static void m_item_string(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_ITEMS) + 6, 0xFFFF); }
static void m_rel_target(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_RELATIONS) + 2, 0xFFFF); }
static void m_cat_range(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_CATEGORIES) + 8, 0xFFFF); }
static void m_cat_string(uint8_t* d, uint32_t n) { (void)n; put32(d + sec_offset(d, PKP_SEC_CATEGORIES), 0xFFFFFFF0u); }
static void m_quiz_answer(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_QUIZ) + 1] = 4; }
static void m_quiz_option(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_QUIZ) + 4, 0xFFFF); }
static void m_meta_string(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_META) + 4, 0xFFFF); }
static void m_meta_count(uint8_t* d, uint32_t n) { (void)n; uint32_t e = dir_entry(d, PKP_SEC_META); put32(d + e + 12, 1); put32(d + e + 8, 6); }
static void m_unknown_sec(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_QUIZ), 0); }
static void m_in_dir(uint8_t* d, uint32_t n) { (void)n; put32(d + dir_entry(d, PKP_SEC_KINDS) + 4, 40); }
static void m_quiz_string(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_QUIZ) + 14, 0xFFFF); }
static void m_quiz_kind(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_QUIZ)] = 9; }
static void m_kind_string(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_KINDS) + 4, 0xFFFF); }
static void m_rel_type(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_RELATIONS)] = 200; }
static void m_overlap(uint8_t* d, uint32_t n) { (void)n; put32(d + dir_entry(d, PKP_SEC_KINDS) + 4, sec_offset(d, PKP_SEC_STRINGS)); }
static void m_strings_all(uint8_t* d, uint32_t n) { uint32_t e = dir_entry(d, PKP_SEC_STRINGS); uint16_t ns = (uint16_t)(d[16] | (d[17] << 8)); put32(d + e + 4, 32 + 16u * ns); put32(d + e + 8, n - 32 - 16u * ns); put32(d + e + 12, n - 32 - 16u * ns); put32(d + 12, 0); }
static void m_strings_flip(uint8_t* d, uint32_t n) { (void)n; d[sec_offset(d, PKP_SEC_STRINGS) + 5] ^= 0x01; }

/* The search index: one key or posting made invalid at a time (sections 10 and 11). */
static uint32_t key_at(const uint8_t* d, uint32_t k) { return sec_offset(d, PKP_SEC_SEARCH_KEYS) + 16 * k; }
static uint32_t nkeys(const uint8_t* d) { return le32(d + dir_entry(d, PKP_SEC_SEARCH_KEYS) + 12); }
static uint32_t run_key; /* a key whose run has two postings or more, found by test_search */
static void m_key_char(uint8_t* d, uint32_t n) { (void)n; d[key_at(d, 0)] = 'A'; }
static void m_key_hole(uint8_t* d, uint32_t n) { (void)n; uint32_t k = key_at(d, 0); d[k] = 'a'; d[k + 1] = 0; d[k + 2] = 'b'; }
static void m_key_empty(uint8_t* d, uint32_t n) { (void)n; memset(d + key_at(d, 0), 0, 10); }
static void m_key_count(uint8_t* d, uint32_t n) { (void)n; put16(d + key_at(d, 0) + 14, 0); }
static void m_key_past(uint8_t* d, uint32_t n) { (void)n; put16(d + key_at(d, nkeys(d) - 1) + 14, 0xFFFF); }
static void m_key_twice(uint8_t* d, uint32_t n) { (void)n; memcpy(d + key_at(d, 1), d + key_at(d, 0), 16); }
static void m_key_gap(uint8_t* d, uint32_t n) { (void)n; put32(d + key_at(d, 0) + 10, 1); }
static void m_key_dropped(uint8_t* d, uint32_t n) { (void)n; uint32_t e = dir_entry(d, PKP_SEC_SEARCH_KEYS); put32(d + e + 12, le32(d + e + 12) - 1); put32(d + e + 8, le32(d + e + 8) - 16); }
static void m_post_object(uint8_t* d, uint32_t n) { (void)n; put16(d + sec_offset(d, PKP_SEC_SEARCH_POSTINGS), 0x7FFF); }
static void m_post_twice(uint8_t* d, uint32_t n) { (void)n; uint32_t p = sec_offset(d, PKP_SEC_SEARCH_POSTINGS) + 2 * le32(d + key_at(d, run_key) + 10); memcpy(d + p + 2, d + p, 2); }
static void m_search_alone(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_SEARCH_POSTINGS), 99); }
static void m_search_esize(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_SEARCH_KEYS) + 2, 15); }
static void m_search_overlap(uint8_t* d, uint32_t n) { (void)n; put32(d + dir_entry(d, PKP_SEC_SEARCH_POSTINGS) + 4, sec_offset(d, PKP_SEC_STRINGS)); }
static void m_search_none(uint8_t* d, uint32_t n) { (void)n; put16(d + dir_entry(d, PKP_SEC_SEARCH_KEYS), 0); put16(d + dir_entry(d, PKP_SEC_SEARCH_POSTINGS), 0); }

/* The search accessors and the table check against the real pack, every way they refuse. */
static void test_search(const uint8_t* pack, uint32_t size) {
    Mem m = {pack, size, -1};
    PkpReader r;
    CHECK(pkp_open(&r, mem_read, &m, size) == PKP_OK);
    CHECK(pkp_has_search(&r));
    uint32_t keys = pkp_count(&r, PKP_SEC_SEARCH_KEYS);
    CHECK(keys > 16 && pkp_count(&r, PKP_SEC_SEARCH_POSTINGS) > 32); /* both tables stream in chunks */
    PkpSearchKey k, prev;
    CHECK(pkp_search_key(&r, 0, &prev) == PKP_OK && prev.first == 0 && prev.len == strlen(prev.key));
    for(uint32_t i = 1; i < keys; i++) {
        CHECK(pkp_search_key(&r, i, &k) == PKP_OK);
        CHECK(strcmp(prev.key, k.key) < 0 && k.first == prev.first + prev.count);
        if(k.count >= 2 && !run_key) run_key = i;
        prev = k;
    }
    CHECK(run_key > 0);
    CHECK(pkp_search_key(&r, keys, &k) == PKP_E_RANGE);
    uint16_t obj;
    bool strong;
    CHECK(pkp_posting(&r, 0, &obj, &strong) == PKP_OK && obj < pkp_count(&r, PKP_SEC_OBJECTS));
    CHECK(pkp_posting(&r, pkp_count(&r, PKP_SEC_SEARCH_POSTINGS), &obj, &strong) == PKP_E_RANGE);
    CHECK(pkp_verify_search(&r) == PKP_OK);
    /* a failed read in each of them, and in each of the check's two streams */
    m.fail_after = 0;
    CHECK(pkp_search_key(&r, 0, &k) == PKP_E_IO);
    m.fail_after = 0;
    CHECK(pkp_posting(&r, 0, &obj, &strong) == PKP_E_IO);
    m.fail_after = 0;
    CHECK(pkp_verify_search(&r) == PKP_E_IO);
    m.fail_after = 1;
    CHECK(pkp_verify_search(&r) == PKP_E_IO);
    m.fail_after = -1;
    PkpReader closed;
    Mem none = {pack, 10, -1};
    CHECK(pkp_open(&closed, mem_read, &none, 10) == PKP_E_TRUNCATED);
    CHECK(!pkp_has_search(&closed) && pkp_verify_search(&closed) == PKP_E_MISSING);

    CHECK(mutated(pack, size, m_key_char, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_key_hole, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_key_empty, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_key_count, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_key_past, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_key_twice, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_key_gap, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_key_dropped, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_post_object, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_post_twice, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_search_alone, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_search_esize, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_search_overlap, 1) == PKP_E_SECTION);
    /* a pack without the index opens, reads and has no search */
    CHECK(mutated(pack, size, m_search_none, 1) == PKP_OK);
    uint8_t* d = malloc(size);
    memcpy(d, pack, size);
    m_search_none(d, size);
    reseal(d, size);
    Mem mm = {d, size, -1};
    CHECK(pkp_open(&r, mem_read, &mm, size) == PKP_OK);
    CHECK(!pkp_has_search(&r) && pkp_verify_search(&r) == PKP_E_MISSING);
    free(d);
}

static void test_reader(const uint8_t* pack, uint32_t size) {
    Mem m = {pack, size, -1};
    PkpReader r;
    CHECK(pkp_open(&r, mem_read, &m, size) == PKP_OK);
    CHECK(pkp_count(&r, PKP_SEC_OBJECTS) > 0);
    CHECK(pkp_count(&r, (PkpSectionType)0) == 0);
    CHECK(pkp_count(&r, (PkpSectionType)99) == 0);
    CHECK(walk(pack, size) == PKP_OK);

    int32_t idx = pkp_find(&r, "kompendium/techniques/ea-1-2-1-falesne-dilema");
    CHECK(idx >= 0);
    CHECK(pkp_find(&r, "no.such.object") == -1);
    char longid[200];
    memset(longid, 'x', sizeof(longid) - 1);
    longid[sizeof(longid) - 1] = '\0';
    CHECK(pkp_find(&r, longid) == -1);

    PkpObject o;
    CHECK(pkp_object(&r, (uint32_t)idx, &o) == PKP_OK);
    char buf[64];
    CHECK(pkp_string(&r, o.title, buf, sizeof(buf)) == PKP_OK);
    CHECK(strcmp(buf, "EA-1.2.1 Falesne dilema") == 0);
    CHECK(pkp_kind_name(&r, o.kind, buf, sizeof(buf)) == PKP_OK && strcmp(buf, "technique") == 0);
    CHECK(o.caps & PKP_CAP_DETECT);
    char tiny[5];
    CHECK(pkp_string(&r, o.title, tiny, sizeof(tiny)) == PKP_OK);
    CHECK(strcmp(tiny, "EA-1") == 0);
    CHECK(pkp_string(&r, o.title, buf, 0) == PKP_E_RANGE);
    PkpStr bad = {0xFFFFFFF0u, 4};
    CHECK(pkp_string(&r, bad, buf, sizeof(buf)) == PKP_E_STRING);
    PkpStr empty = {0, 0};
    CHECK(pkp_string(&r, empty, buf, sizeof(buf)) == PKP_OK && buf[0] == '\0');

    PkpItem it;
    CHECK(pkp_item(&r, o.items_first, &it) == PKP_OK && it.type == PKP_ITEM_INDICATOR);
    PkpRelation rel;
    CHECK(pkp_relation(&r, o.rel_first, &rel) == PKP_OK);
    CHECK(pkp_relation_name(&r, rel.type, buf, sizeof(buf)) == PKP_OK);
    CHECK(strcmp(buf, "confused_with") == 0);
    CHECK(pkp_relation_name(&r, 0, buf, sizeof(buf)) == PKP_E_RANGE && buf[0] == '\0');
    CHECK(pkp_kind_name(&r, 0, buf, sizeof(buf)) == PKP_E_RANGE);
    CHECK(pkp_kind_name(&r, 200, buf, sizeof(buf)) == PKP_E_RANGE);
    CHECK(pkp_kind_name(&r, 1, buf, 0) == PKP_E_RANGE);
    CHECK(pkp_verify_strings(&r) == PKP_OK);
    CHECK(pkp_object(&r, 0xFFFFFFFFu, &o) == PKP_E_RANGE);
    CHECK(pkp_item(&r, 0xFFFFFFFFu, &it) == PKP_E_RANGE);
    CHECK(pkp_relation(&r, 0xFFFFFFFFu, &rel) == PKP_E_RANGE);
    PkpCategory c;
    CHECK(pkp_category(&r, 0xFFFF, &c) == PKP_E_RANGE);
    PkpQuiz q;
    CHECK(pkp_quiz(&r, 0xFFFFFFFFu, &q) == PKP_E_RANGE);
    PkpStr s;
    CHECK(pkp_meta(&r, PKP_META_COUNT, &s) == PKP_E_RANGE);
    CHECK(pkp_meta(&r, PKP_META_SOURCE_COMMIT, &s) == PKP_OK);
    CHECK(pkp_string(&r, s, buf, sizeof(buf)) == PKP_OK && strlen(buf) == 40);

    CHECK(pkp_quiz(&r, 0, &q) == PKP_OK);
    CHECK(pocket_quiz_correct(&q, q.answer));
    CHECK(!pocket_quiz_correct(&q, (uint8_t)((q.answer + 1) % 4)));
    CHECK(!pocket_quiz_correct(&q, 7));

    for(int s2 = 0; s2 <= PKP_STATUS_COUNT; s2++) CHECK(pkp_status_text((PkpStatus)s2)[0]);
}

static void test_io_failures(const uint8_t* pack, uint32_t size) {
    /* Fail the n-th read for every n the open path performs; each must report PKP_E_IO. */
    int io_seen = 0;
    for(int n = 0; n < 2000; n++) {
        Mem m = {pack, size, n};
        PkpReader r;
        PkpStatus st = pkp_open(&r, mem_read, &m, size);
        if(st == PKP_OK) break;
        CHECK(st == PKP_E_IO);
        io_seen++;
    }
    CHECK(io_seen > 3);
    Mem m = {pack, size, -1};
    PkpReader r;
    CHECK(pkp_open(&r, mem_read, &m, size) == PKP_OK);
    m.fail_after = 0;
    PkpObject o;
    PkpItem it;
    PkpRelation rel;
    PkpCategory c;
    PkpQuiz q;
    PkpStr s;
    char buf[16];
    CHECK(pkp_object(&r, 0, &o) == PKP_E_IO);
    CHECK(pkp_item(&r, 0, &it) == PKP_E_IO);
    CHECK(pkp_relation(&r, 0, &rel) == PKP_E_IO);
    CHECK(pkp_category(&r, 0, &c) == PKP_E_IO);
    CHECK(pkp_quiz(&r, 0, &q) == PKP_E_IO);
    CHECK(pkp_meta(&r, 0, &s) == PKP_E_IO);
    PkpStr one = {0, 4};
    CHECK(pkp_string(&r, one, buf, sizeof(buf)) == PKP_E_IO);
    CHECK(pkp_find(&r, "kompendium/techniques/ea-1-2-1-falesne-dilema") == -1);
    CHECK(pkp_verify_strings(&r) == PKP_E_IO);
    CHECK(pkp_kind_name(&r, 1, buf, sizeof(buf)) == PKP_E_IO);
}

static void test_corruption(const uint8_t* pack, uint32_t size) {
    CHECK(walk(pack, 10) == PKP_E_TRUNCATED);
    CHECK(mutated(pack, size, m_magic, 0) == PKP_E_MAGIC);
    CHECK(mutated(pack, size, m_version, 0) == PKP_E_VERSION);
    CHECK(mutated(pack, size, m_features, 0) == PKP_E_FEATURES);
    CHECK(mutated(pack, size, m_size, 0) == PKP_E_SIZE);
    CHECK(mutated(pack, size, m_flip, 0) == PKP_E_CHECKSUM);
    CHECK(mutated(pack, size, m_nsec, 1) == PKP_E_DIRECTORY);
    CHECK(mutated(pack, size, m_dir_past_end, 1) == PKP_E_DIRECTORY);
    CHECK(mutated(pack, size, m_sec_overflow, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_entry_size, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_count, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_missing, 1) == PKP_E_MISSING);
    CHECK(mutated(pack, size, m_objcount, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_obj_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_obj_kind, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_obj_items, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_obj_category, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_item_type, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_item_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_rel_target, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_cat_range, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_cat_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_quiz_answer, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_quiz_option, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_meta_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_meta_count, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_unknown_sec, 1) == PKP_E_MISSING);
    CHECK(mutated(pack, size, m_in_dir, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_overlap, 1) == PKP_E_SECTION);
    CHECK(mutated(pack, size, m_strings_all, 0) == PKP_E_SECTION); /* review finding 1 */
    CHECK(mutated(pack, size, m_quiz_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_quiz_kind, 1) == PKP_E_RANGE);
    CHECK(mutated(pack, size, m_kind_string, 1) == PKP_E_STRING);
    CHECK(mutated(pack, size, m_rel_type, 1) == PKP_E_RANGE);
    /* The string pool is outside the index CRC: open succeeds, the explicit check fails. */
    CHECK(mutated(pack, size, m_strings_flip, 0) == PKP_OK);
    uint8_t* d = malloc(size);
    memcpy(d, pack, size);
    m_strings_flip(d, size);
    Mem mm = {d, size, -1};
    PkpReader rr;
    CHECK(pkp_open(&rr, mem_read, &mm, size) == PKP_OK);
    CHECK(pkp_verify_strings(&rr) == PKP_E_CHECKSUM);
    d[sec_offset(d, PKP_SEC_STRINGS)] = 0x00;
    d[sec_offset(d, PKP_SEC_STRINGS) + 1] = 0xFF;
    PkpStr two = {0, 2};
    char sb[4];
    CHECK(pkp_string(&rr, two, sb, sizeof(sb)) == PKP_OK && strcmp(sb, "??") == 0);
    PkpReader closed;
    Mem bad = {d, 10, -1};
    CHECK(pkp_open(&closed, mem_read, &bad, 10) == PKP_E_TRUNCATED);
    PkpObject co;
    CHECK(pkp_object(&closed, 0, &co) == PKP_E_RANGE);
    CHECK(pkp_string(&closed, two, sb, sizeof(sb)) == PKP_E_RANGE);
    CHECK(pkp_verify_strings(&closed) == PKP_E_RANGE);
    free(d);
}

static void test_fuzz(const uint8_t* pack, uint32_t size) {
    /* Deterministic mutation fuzz: random bytes rewritten, CRC re-sealed so the structural
     * checks are what stands between the mutation and memory. ASan/UBSan watch every run. */
    uint8_t* d = malloc(size);
    uint32_t x = 0x9E3779B9u, accepted = 0, rejected = 0;
    for(int round = 0; round < FUZZ_ROUNDS; round++) {
        memcpy(d, pack, size);
        int edits = 1 + round % 6;
        for(int e = 0; e < edits; e++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            uint32_t at = (round & 1) ? 32 + x % 512 : x % size; /* bias toward the directory */
            d[at % size] = (uint8_t)(x >> 24);
        }
        reseal(d, size);
        if(walk(d, size) == PKP_OK) accepted++;
        else rejected++;
    }
    free(d);
    printf("fuzz: %d mutated packs,", FUZZ_ROUNDS); printf(" %u accepted, %u rejected, no memory error\n", accepted,
           rejected);
    CHECK(rejected > 0);
}

static void test_detect(void) {
    uint8_t answers[] = {POCKET_OBSERVED, POCKET_OBSERVED, POCKET_NOT_OBSERVED, POCKET_UNKNOWN,
                         POCKET_NOT_APPLICABLE, POCKET_UNANSWERED, 42};
    PocketSummary s;
    pocket_summarize(answers, sizeof(answers), &s);
    CHECK(s.total == 7);
    CHECK(s.counts[POCKET_OBSERVED] == 2);
    CHECK(s.counts[POCKET_UNANSWERED] == 2); /* out-of-range answers count as unanswered */
    char buf[160];
    size_t n = pocket_summary_text(&s, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strcmp(buf, "Observed 2 of 7 markers\nnot observed: 1\nunknown: 1\n"
                      "not applicable: 1\nunanswered: 2") == 0);
    char small[10];
    n = pocket_summary_text(&s, small, sizeof(small));
    CHECK(n == 9 && strlen(small) == 9);
    CHECK(pocket_summary_text(&s, small, 0) == 0);
    PocketSummary none;
    pocket_summarize(answers, 0, &none);
    CHECK(pocket_summary_text(&none, buf, sizeof(buf)) == strlen("Observed 0 of 0 markers"));
    CHECK(strcmp(pocket_answer_name(POCKET_UNKNOWN), "unknown") == 0);
    CHECK(strcmp(pocket_answer_name(200), "invalid") == 0);
}

static void json_str(const PkpReader* r, PkpStr s) {
    static char buf[PKP_MAX_STRING + 1];
    pkp_string(r, s, buf, sizeof(buf));
    putchar('"');
    for(const char* p = buf; *p; p++) {
        if(*p == '"' || *p == '\\') printf("\\%c", *p);
        else if((unsigned char)*p < 0x20) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static int dump(const uint8_t* d, uint32_t size) {
    static const char* const metas[] = {"pack_id", "title", "version", "source_repository",
                                        "source_commit", "generator", "licence"};
    /* sorted by name, as the Python reference prints them */
    static const char* const caps[] = {"apply", "assess", "browse", "compare",
                                       "detect", "quiz", "score", "simulate"};
    static const uint8_t capbit[] = {PKP_CAP_APPLY, PKP_CAP_ASSESS, PKP_CAP_BROWSE,
                                     PKP_CAP_COMPARE, PKP_CAP_DETECT, PKP_CAP_QUIZ,
                                     PKP_CAP_SCORE, PKP_CAP_SIMULATE};
    Mem m = {d, size, -1};
    PkpReader r;
    PkpStatus st = pkp_open(&r, mem_read, &m, size);
    if(st != PKP_OK) {
        fprintf(stderr, "open: %s\n", pkp_status_text(st));
        return 1;
    }
    printf("{\"meta\":{");
    for(uint32_t i = 0; i < PKP_META_COUNT; i++) {
        PkpStr s;
        pkp_meta(&r, i, &s);
        printf("%s\"%s\":", i ? "," : "", metas[i]);
        json_str(&r, s);
    }
    printf("},\"categories\":[");
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_CATEGORIES); i++) {
        PkpCategory c;
        pkp_category(&r, i, &c);
        printf("%s{\"name\":", i ? "," : "");
        json_str(&r, c.name);
        printf(",\"first\":%u,\"count\":%u}", c.first, c.count);
    }
    printf("],\"objects\":[");
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_OBJECTS); i++) {
        PkpObject o;
        pkp_object(&r, i, &o);
        printf("%s{\"id\":", i ? "," : "");
        json_str(&r, o.id);
        printf(",\"code\":");
        json_str(&r, o.code);
        printf(",\"title\":");
        json_str(&r, o.title);
        printf(",\"definition\":");
        json_str(&r, o.definition);
        char kname[64];
        pkp_kind_name(&r, o.kind, kname, sizeof(kname));
        printf(",\"category\":%u,\"kind\":\"%s\",\"caps\":[", o.category, kname);
        int first = 1;
        for(size_t k = 0; k < sizeof(capbit); k++) {
            if(o.caps & capbit[k]) {
                printf("%s\"%s\"", first ? "" : ",", caps[k]);
                first = 0;
            }
        }
        printf("],\"items\":[");
        for(uint32_t k = 0; k < o.items_count; k++) {
            PkpItem it;
            pkp_item(&r, o.items_first + k, &it);
            printf("%s{\"type\":%u,\"text\":", k ? "," : "", it.type);
            json_str(&r, it.text);
            putchar('}');
        }
        printf("],\"relations\":[");
        for(uint32_t k = 0; k < o.rel_count; k++) {
            PkpRelation rel;
            pkp_relation(&r, o.rel_first + k, &rel);
            char rname[64];
            pkp_relation_name(&r, rel.type, rname, sizeof(rname));
            printf("%s{\"type\":\"%s\",\"target\":%u}", k ? "," : "", rname, rel.target);
        }
        printf("]}");
    }
    printf("],\"quiz\":[");
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_QUIZ); i++) {
        PkpQuiz q;
        pkp_quiz(&r, i, &q);
        printf("%s{\"kind\":%u,\"answer\":%u,\"options\":[%u,%u,%u,%u],\"prompt\":", i ? "," : "",
               q.kind, q.answer, q.options[0], q.options[1], q.options[2], q.options[3]);
        json_str(&r, q.prompt);
        printf(",\"context\":");
        json_str(&r, q.context);
        printf(",\"explanation\":");
        json_str(&r, q.explanation);
        printf(",\"response\":");
        json_str(&r, q.response);
        putchar('}');
    }
    printf("],\"search\":");
    if(!pkp_has_search(&r)) {
        printf("null}\n");
        return 0;
    }
    putchar('[');
    for(uint32_t i = 0; i < pkp_count(&r, PKP_SEC_SEARCH_KEYS); i++) {
        PkpSearchKey k;
        pkp_search_key(&r, i, &k);
        printf("%s{\"key\":\"%s\",\"postings\":[", i ? "," : "", k.key);
        for(uint32_t j = 0; j < k.count; j++) {
            uint16_t obj;
            bool strong;
            pkp_posting(&r, k.first + j, &obj, &strong);
            printf("%s[%u,%d]", j ? "," : "", obj, strong ? 1 : 0);
        }
        printf("]}");
    }
    printf("]}\n");
    return 0;
}

static int walk_list(const char* dir) {
    char path[1024], line[512];
    snprintf(path, sizeof(path), "%s/list.txt", dir);
    FILE* f = fopen(path, "r");
    if(!f) {
        perror(path);
        return 2;
    }
    while(fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        snprintf(path, sizeof(path), "%s/%s", dir, line);
        uint32_t size;
        uint8_t* d = load(path, &size);
        PkpStatus st = walk(d, size);
        printf("%s %s\n", line, st == PKP_OK ? "ok" : "err");
        free(d);
    }
    fclose(f);
    return 0;
}

/* The block cache returns exactly the bytes of the file for any offset and length, counts its
 * hits and misses, evicts the least recently used block, and caches nothing a failed read
 * returned. A whole pack opened through it reads the same objects as without it. */
static void test_cache(const uint8_t* pack, uint32_t size) {
    Mem m = {pack, size, -1};
    PkpCache c;
    pkp_cache_init(&c, mem_read, &m, size);
    uint8_t got[700];

    /* bytes 200..799 span four blocks: four misses, then the same read hits all four */
    CHECK(pkp_cache_read(&c, 200, got, 600));
    CHECK(memcmp(got, pack + 200, 600) == 0);
    CHECK(c.misses == 4 && c.hits == 0);
    CHECK(pkp_cache_read(&c, 200, got, 600));
    CHECK(c.misses == 4 && c.hits == 4);

    /* out of range and zero-length requests */
    CHECK(!pkp_cache_read(&c, size, got, 1));
    CHECK(!pkp_cache_read(&c, size + 1, got, 0));
    CHECK(pkp_cache_read(&c, size, got, 0));
    CHECK(!pkp_cache_read(&c, size - 4, got, 8));
    /* the last, short block */
    CHECK(pkp_cache_read(&c, size - 4, got, 4));
    CHECK(memcmp(got, pack + size - 4, 4) == 0);

    /* LRU: fill every slot, touch block 0, then a new block evicts block 1, not block 0 */
    pkp_cache_clear(&c);
    for(uint32_t b = 0; b < PKP_CACHE_BLOCKS; b++)
        CHECK(pkp_cache_read(&c, b * PKP_CACHE_BLOCK, got, 1));
    CHECK(pkp_cache_read(&c, 0, got, 1));
    uint32_t misses = c.misses;
    CHECK(pkp_cache_read(&c, PKP_CACHE_BLOCKS * PKP_CACHE_BLOCK, got, 1));
    CHECK(c.misses == misses + 1);
    CHECK(pkp_cache_read(&c, 0, got, 1));
    CHECK(c.misses == misses + 1);
    CHECK(pkp_cache_read(&c, PKP_CACHE_BLOCK, got, 1));
    CHECK(c.misses == misses + 2);

    /* a failed underlying read is reported and leaves nothing cached */
    pkp_cache_clear(&c);
    m.fail_after = 0;
    CHECK(!pkp_cache_read(&c, 0, got, 1));
    m.fail_after = -1;
    CHECK(pkp_cache_read(&c, 0, got, 16));
    CHECK(memcmp(got, pack, 16) == 0);

    /* differential: random reads equal the file */
    uint32_t seed = 12345;
    for(int i = 0; i < 4000; i++) {
        seed = seed * 1103515245u + 12345u;
        uint32_t off = seed % size;
        seed = seed * 1103515245u + 12345u;
        uint32_t len = seed % sizeof(got);
        if(len > size - off) len = size - off;
        if(!pkp_cache_read(&c, off, got, len) || memcmp(got, pack + off, len) != 0) {
            CHECK(0);
            break;
        }
    }

    /* the reader through the cache */
    PkpReader r;
    pkp_cache_init(&c, mem_read, &m, size);
    CHECK(pkp_open(&r, pkp_cache_read, &c, size) == PKP_OK);
    const char* id = "kompendium/techniques/ea-1-2-1-falesne-dilema";
    int32_t idx = pkp_find(&r, id);
    CHECK(idx >= 0);
    /* the same lookup again is served from memory */
    uint32_t cold = c.misses;
    CHECK(pkp_find(&r, id) == idx);
    CHECK(c.misses == cold);
}

/* Leitner progress: boxes and due sessions, the file's round trip, every way a file can be
 * bad, recovery from two copies, and a full table. */
static void test_progress(void) {
    static Progress p, q;
    static uint8_t a[PROGRESS_MAX_FILE], b[PROGRESS_MAX_FILE];
    CHECK(progress_key("abc") == progress_key("abc"));
    CHECK(progress_key("abc") != progress_key("abd"));
    CHECK(progress_key("") == 2166136261u);

    progress_init(&p);
    p.session = 3;
    progress_record(&p, 7, 10, true); /* new, right: box 2, due in 2 sessions */
    CHECK(p.count == 1 && p.entries[0].box == 2 && p.entries[0].due == 5);
    CHECK(p.entries[0].attempts == 1 && p.entries[0].last_right == 1);
    progress_record(&p, 7, 11, true); /* box 3, due in 4; the index follows the pack */
    CHECK(p.entries[0].box == 3 && p.entries[0].due == 7 && p.entries[0].quiz == 11);
    for(int i = 0; i < 5; i++)
        progress_record(&p, 7, 11, true);
    CHECK(p.entries[0].box == PROGRESS_BOXES);
    progress_record(&p, 7, 11, false); /* wrong: back to box 1, due next session */
    CHECK(p.entries[0].box == 1 && p.entries[0].due == 4 && p.entries[0].last_right == 0);
    progress_record(&p, 8, 12, false);
    CHECK(progress_due_count(&p) == 0);
    p.session = 4;
    CHECK(progress_due_count(&p) == 2);
    progress_record(&p, 9, 13, true); /* box 2: not due yet */
    CHECK(progress_due_count(&p) == 2);
    int next = progress_next_due(&p);
    CHECK(next >= 0 && p.entries[next].box == 1);
    p.entries[1].due = 2; /* longer overdue in the same box wins */
    CHECK(progress_next_due(&p) == 1);
    p.session = 5;
    CHECK(progress_next_due(&p) == 1); /* box 1 before box 2 */

    /* round trip */
    size_t n = progress_encode(&p, a, sizeof(a));
    CHECK(n == PROGRESS_HEADER + 3 * PROGRESS_ENTRY + 4 && p.generation == 1);
    CHECK(progress_decode(&q, a, n) == PROGRESS_OK);
    CHECK(q.count == 3 && q.session == 5 && q.generation == 1);
    CHECK(memcmp(q.entries, p.entries, sizeof(ProgressEntry) * 3) == 0);
    CHECK(progress_encode(&p, a, 10) == 0); /* too small */

    /* every way a file is bad leaves the progress empty */
    CHECK(progress_decode(&q, a, 10) == PROGRESS_E_SHORT && q.count == 0);
    memcpy(b, a, n);
    b[0] = 'X';
    CHECK(progress_decode(&q, b, n) == PROGRESS_E_MAGIC);
    memcpy(b, a, n);
    b[4] = 9;
    CHECK(progress_decode(&q, b, n) == PROGRESS_E_VERSION);
    memcpy(b, a, n);
    b[14] = 0xFF;
    b[15] = 0xFF;
    CHECK(progress_decode(&q, b, n) == PROGRESS_E_COUNT);
    CHECK(progress_decode(&q, a, n - 5) == PROGRESS_E_SHORT); /* cut mid-write */
    memcpy(b, a, n);
    b[PROGRESS_HEADER] ^= 1;
    CHECK(progress_decode(&q, b, n) == PROGRESS_E_CRC);
    /* an out-of-range box in a checksummed file is read as box 1 */
    memcpy(b, a, n);
    b[PROGRESS_HEADER + 6] = 9;
    uint32_t crc = pkp_crc32(0, b, n - 4);
    b[n - 4] = (uint8_t)crc;
    b[n - 3] = (uint8_t)(crc >> 8);
    b[n - 2] = (uint8_t)(crc >> 16);
    b[n - 1] = (uint8_t)(crc >> 24);
    CHECK(progress_decode(&q, b, n) == PROGRESS_OK && q.entries[0].box == 1);

    /* two copies: the valid one with the higher generation wins */
    size_t m = progress_encode(&p, b, sizeof(b)); /* generation 2 */
    CHECK(progress_load(&q, a, n, b, m) == 2 && q.generation == 2);
    CHECK(progress_load(&q, b, m, a, n) == 1 && q.generation == 2);
    CHECK(progress_load(&q, a, n, b, m - 1) == 1 && q.generation == 1); /* tmp cut short */
    CHECK(progress_load(&q, NULL, 0, b, m) == 2);
    CHECK(progress_load(&q, a, 3, NULL, 0) == 0 && q.count == 0);

    /* a full table replaces the entry due furthest away */
    progress_init(&p);
    for(uint32_t k = 0; k < PROGRESS_CAP; k++)
        progress_record(&p, k + 100, (uint16_t)k, false);
    p.entries[17].due = 900;
    progress_record(&p, 99999, 1, false);
    CHECK(p.count == PROGRESS_CAP && p.entries[17].key == 99999);
    progress_drop(&p, 0);
    CHECK(p.count == PROGRESS_CAP - 1);
    CHECK(progress_next_due(&p) == -1); /* nothing due in session 0 */
}

/* Saved sessions: a line round-trips, quotes cannot break it, a short buffer refuses it, and
 * anything but a whole line written by session_line is rejected. */
static void test_session(void) {
    static char line[SESSION_MAX_LINE];
    static const uint8_t a[] = {POCKET_OBSERVED, POCKET_NOT_OBSERVED, POCKET_UNKNOWN,
                                POCKET_NOT_APPLICABLE, POCKET_UNANSWERED, 99};
    static const uint8_t b[] = {POCKET_OBSERVED, POCKET_OBSERVED};
    SessionCand c[2] = {{"kompendium/x", 173, a, 6}, {"kompendium/y\"q", 9, b, 2}};
    SessionView v;
    size_t n = session_line(line, sizeof(line), "2026-11-25T12:00:00", "pack \"1\"", c, 2);
    CHECK(n > 0 && line[n - 1] == '\n');
    CHECK(strstr(line, "\"m\":\"ONUA--\"") != NULL);
    CHECK(strstr(line, "pack ?1?") != NULL && strstr(line, "y?q") != NULL);
    CHECK(session_parse(line, n - 1, &v));
    CHECK(strcmp(v.time, "2026-11-25T12:00:00") == 0 && v.cands == 2);
    CHECK(strcmp(v.id[0], "kompendium/x") == 0 && v.index[0] == 173);
    CHECK(v.observed[0] == 1 && v.total[0] == 6 && v.observed[1] == 2 && v.total[1] == 2);
    CHECK(session_line(line, 20, "t", "p", c, 2) == 0); /* does not fit */
    n = session_line(line, sizeof(line), "t", "p", c, 1);
    CHECK(session_parse(line, n - 1, &v) && v.cands == 1);
    CHECK(!session_parse(line, n - 5, &v)); /* torn */
    CHECK(!session_parse("{\"v\":2,", 7, &v));
    CHECK(!session_parse("x", 1, &v));
    CHECK(!session_parse("{\"v\":1,\"x\":1}", 13, &v)); /* no time */
    CHECK(!session_parse("{\"v\":1,\"t\":\"a", 14, &v)); /* time never closed */
    CHECK(!session_parse("{\"v\":1,\"t\":1}", 13, &v)); /* time not a string */
    CHECK(!session_parse("{\"v\":1,\"t\":\"a\",\"c\":[]}", 22, &v)); /* no candidates */
    CHECK(!session_parse("{\"v\":1,\"t\":\"a\",\"c\":[{\"id\":\"x\",\"q\":1}]}", 35, &v));
    CHECK(!session_parse("{\"v\":1,\"t\":\"a\",\"c\":[{\"id\":\"x\",\"i\":1}]}", 35, &v));
}

/* The crisis simulation's rules, case by case as the source orders them. */
static void test_simulate(void) {
    SimState s;
    sim_start(&s, 150);
    CHECK(s.trust == 100);
    sim_start(&s, -5);
    CHECK(s.trust == 0);
    sim_start(&s, 50);
    sim_apply(&s, "positive", 15);
    sim_apply(&s, "positive", 10);
    sim_apply(&s, "neutral", 0);
    CHECK(s.trust == 75 && s.positive == 2 && s.negative == 0 && s.decisions == 3);
    CHECK(sim_outcome(&s) == SIM_OPTIMAL);
    sim_apply(&s, "negative", -10);
    sim_apply(&s, "critical", -5);
    CHECK(s.trust == 60 && s.negative == 2);
    CHECK(sim_outcome(&s) == SIM_ACCEPTABLE); /* 2 positive, 2 negative, trust 60 */
    sim_apply(&s, "negative", -100);
    CHECK(s.trust == 0 && sim_outcome(&s) == SIM_CRITICAL_FAILURE);
    sim_start(&s, 30);
    sim_apply(&s, "negative", 0);
    CHECK(sim_outcome(&s) == SIM_SUBOPTIMAL); /* trust 30: neither >= 40 nor < 20 */
    sim_start(&s, 80);
    sim_apply(&s, "negative", 0);
    sim_apply(&s, "positive", 0);
    CHECK(sim_outcome(&s) == SIM_ACCEPTABLE); /* trust 80 but positive == negative */
    CHECK(strcmp(sim_outcome_name(SIM_CRITICAL_FAILURE), "Critical failure") == 0);
    char text[] = "low|positive|15|success|Ask how they are.|They calm down.";
    SimOption o;
    CHECK(sim_option_split(text, &o));
    CHECK(strcmp(o.risk, "low") == 0 && strcmp(o.impact, "positive") == 0 && o.delta == 15);
    CHECK(strcmp(o.type, "success") == 0 && strcmp(o.consequence, "They calm down.") == 0);
    char bad[] = "low|positive|15";
    CHECK(!sim_option_split(bad, &o));
}

/* Scorecard pieces: the setup, a choice, a band, and the clamp. */
static void test_scorecard(void) {
    int base, min, max;
    CHECK(score_setup("50|0|100", &base, &min, &max) && base == 50 && max == 100);
    CHECK(!score_setup("50|0", &base, &min, &max));
    CHECK(!score_setup("50|9|1", &base, &min, &max)); /* min above max */
    char c[] = "-15|Yes|Founded it.";
    ScoreChoice ch;
    CHECK(score_choice(c, &ch) && ch.weight == -15 && strcmp(ch.label, "Yes") == 0);
    CHECK(strcmp(ch.note, "Founded it.") == 0);
    char bad[] = "10|Yes";
    CHECK(!score_choice(bad, &ch));
    char b[] = "20|A|Low Risk|";
    ScoreBand band;
    CHECK(score_band(b, &band) && band.top == 20 && strcmp(band.code, "A") == 0);
    CHECK(strcmp(band.label, "Low Risk") == 0 && band.meaning[0] == '\0');
    char bad2[] = "20|A";
    CHECK(!score_band(bad2, &band));
    CHECK(score_clamp(130, 0, 100) == 100 && score_clamp(-5, 0, 100) == 0);
    CHECK(score_clamp(42, 0, 100) == 42);
}

/* Bayesian fusion as the source's code computes it: 0.8, 0.7, 0.9 from 0.5 give 0.9882 (the
 * source's docstring says 0.976, which its own code does not produce). */
static void test_bayes(void) {
    static const uint8_t three[] = {80, 70, 90};
    float p = bayes_fuse(0.5f, three, 3);
    CHECK(p > 0.9882f && p < 0.9883f);
    CHECK(bayes_fuse(0.5f, three, 0) == 0.5f);
    CHECK(bayes_update(0.0f, 0.0f) == 0.0f); /* a zero denominator leaves the belief */
    static const uint8_t against[] = {10};
    CHECK(bayes_fuse(0.5f, against, 1) < 0.11f);
    CHECK(strcmp(bayes_status(95, 90, 10), "confirmed") == 0);
    CHECK(strcmp(bayes_status(5, 90, 10), "rejected") == 0);
    CHECK(strcmp(bayes_status(50, 90, 10), "tentative") == 0);
    CalcSpec spec;
    CHECK(calc_parse("bayes|50|90|10|10,30,95", &spec));
    CHECK(strcmp(spec.algorithm, "bayes") == 0 && spec.prior == 50 && spec.n_choices == 3);
    CHECK(spec.choices[2] == 95);
    CHECK(!calc_parse("bayes|50|90", &spec));
    CHECK(!calc_parse("bayes|500|90|10|50", &spec));
    CHECK(!calc_parse("bayes|50|90|10|", &spec));
    CHECK(!calc_parse("bayes|50|90|10|x", &spec));
    CHECK(!calc_parse("bayes|50|90|10|100", &spec));
}

int main(int argc, char** argv) {
    if(argc != 3) {
        fprintf(stderr, "usage: test_pkp test|dump|walk <path>\n");
        return 2;
    }
    if(strcmp(argv[1], "walk") == 0) return walk_list(argv[2]);
    uint32_t size;
    uint8_t* pack = load(argv[2], &size);
    int rc;
    if(strcmp(argv[1], "dump") == 0) {
        rc = dump(pack, size);
    } else {
        test_reader(pack, size);
        test_io_failures(pack, size);
        test_corruption(pack, size);
        test_search(pack, size);
        test_fuzz(pack, size);
        test_detect();
        test_cache(pack, size);
        test_progress();
        test_session();
        test_simulate();
        test_scorecard();
        test_bayes();
        printf("native: %s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
        rc = failures ? 1 : 0;
    }
    free(pack);
    return rc;
}
