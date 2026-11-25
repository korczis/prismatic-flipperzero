#include "pkp.h"

#include <string.h>

static const uint8_t pkp_entry_size[PKP_SEC_KNOWN_LAST + 1] = {
    [PKP_SEC_META] = 6,
    [PKP_SEC_STRINGS] = 1,
    [PKP_SEC_CATEGORIES] = 10,
    [PKP_SEC_OBJECTS] = 40,
    [PKP_SEC_ITEMS] = 8,
    [PKP_SEC_RELATIONS] = 4,
    [PKP_SEC_QUIZ] = 34,
    [PKP_SEC_KINDS] = 6,
    [PKP_SEC_RELNAMES] = 6,
};

static const char* const pkp_status[PKP_STATUS_COUNT] = {
    [PKP_OK] = "ok",
    [PKP_E_IO] = "read error",
    [PKP_E_TRUNCATED] = "file too short",
    [PKP_E_MAGIC] = "not a PKP1 pack",
    [PKP_E_VERSION] = "unsupported pack version",
    [PKP_E_FEATURES] = "pack needs newer app",
    [PKP_E_SIZE] = "size mismatch",
    [PKP_E_CHECKSUM] = "pack corrupted (CRC)",
    [PKP_E_DIRECTORY] = "bad section table",
    [PKP_E_SECTION] = "bad section",
    [PKP_E_MISSING] = "section missing",
    [PKP_E_RANGE] = "index out of range",
    [PKP_E_STRING] = "bad string",
};

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static PkpStr rdstr(const uint8_t* p) {
    PkpStr s = {rd32(p), rd16(p + 4)};
    return s;
}

uint32_t pkp_crc32(uint32_t crc, const uint8_t* data, size_t len) {
    static uint32_t table[256];
    if(table[1] == 0) {
        for(uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            table[i] = c;
        }
    }
    crc = ~crc;
    for(size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* CRC of the file range [from, to), read in chunks. */
static bool crc_range(PkpReadFn read, void* ctx, uint32_t from, uint32_t to, uint32_t* crc) {
    uint8_t chunk[512];
    for(uint32_t at = from; at < to;) {
        uint32_t n = to - at < sizeof(chunk) ? to - at : (uint32_t)sizeof(chunk);
        if(!read(ctx, at, chunk, n)) return false;
        *crc = pkp_crc32(*crc, chunk, n);
        at += n;
    }
    return true;
}

/* Reads entry `index` of section `type` into buf (entry size bytes). */
static PkpStatus read_entry(const PkpReader* r, PkpSectionType type, uint32_t index, uint8_t* buf) {
    const PkpSection* s = &r->sec[type];
    if(r->size == 0 || index >= s->count) return PKP_E_RANGE; /* closed or failed open */
    uint32_t size = pkp_entry_size[type];
    if(!r->read(r->ctx, s->offset + index * size, buf, size)) return PKP_E_IO;
    return PKP_OK;
}

static bool str_ok(const PkpReader* r, PkpStr s) {
    return s.len <= PKP_MAX_STRING &&
           (uint64_t)s.off + s.len <= (uint64_t)r->sec[PKP_SEC_STRINGS].length;
}

PkpStatus pkp_open(PkpReader* r, PkpReadFn read, void* ctx, uint32_t file_size) {
    memset(r, 0, sizeof(*r));
    r->read = read;
    r->ctx = ctx;
    uint8_t h[PKP_HEADER_SIZE];
    if(file_size < PKP_HEADER_SIZE) return PKP_E_TRUNCATED;
    if(!read(ctx, 0, h, PKP_HEADER_SIZE)) return PKP_E_IO;
    if(memcmp(h, "PKP1", 4) != 0) return PKP_E_MAGIC;
    if(rd16(h + 4) != PKP_FORMAT_VERSION) return PKP_E_VERSION;
    if(rd16(h + 6) != 0) return PKP_E_FEATURES;
    if(rd32(h + 8) != file_size) return PKP_E_SIZE;
    uint32_t want_crc = rd32(h + 12);
    uint16_t nsec = rd16(h + 16);
    uint32_t nobj = rd32(h + 20);
    r->strings_crc = rd32(h + 24);

    uint32_t dir_end = PKP_HEADER_SIZE + (uint32_t)nsec * PKP_SECTION_SIZE;
    if(nsec > PKP_MAX_SECTIONS || dir_end > file_size) return PKP_E_DIRECTORY;
    for(uint16_t k = 0; k < nsec; k++) {
        uint8_t e[PKP_SECTION_SIZE];
        if(!read(ctx, PKP_HEADER_SIZE + (uint32_t)k * PKP_SECTION_SIZE, e, sizeof(e))) {
            return PKP_E_IO;
        }
        uint16_t type = rd16(e);
        uint16_t esize = rd16(e + 2);
        PkpSection s = {rd32(e + 4), rd32(e + 8), rd32(e + 12)};
        if((uint64_t)s.offset + s.length > file_size || s.offset < dir_end) return PKP_E_SECTION;
        if(type == 0 || type > PKP_SEC_KNOWN_LAST) continue; /* optional, unknown: skip */
        if(esize != pkp_entry_size[type] || (uint64_t)esize * s.count != s.length) {
            return PKP_E_SECTION;
        }
        r->sec[type] = s;
        r->present |= 1u << type;
    }
    for(int t = PKP_SEC_META; t <= PKP_SEC_KNOWN_LAST; t++) {
        if(!(r->present & (1u << t))) return PKP_E_MISSING;
    }
    /* No two known sections may overlap: otherwise a table could sit inside the string pool,
     * outside the index CRC (review finding, 2026-11-25). */
    for(int a = PKP_SEC_META; a <= PKP_SEC_KNOWN_LAST; a++) {
        for(int b = a + 1; b <= PKP_SEC_KNOWN_LAST; b++) {
            const PkpSection* x = &r->sec[a];
            const PkpSection* y = &r->sec[b];
            if(x->length && y->length && x->offset < (uint64_t)y->offset + y->length &&
               y->offset < (uint64_t)x->offset + x->length) {
                return PKP_E_SECTION;
            }
        }
    }

    const PkpSection* str = &r->sec[PKP_SEC_STRINGS];
    uint32_t crc = 0;
    if(!crc_range(read, ctx, PKP_HEADER_SIZE, str->offset, &crc) ||
       !crc_range(read, ctx, str->offset + str->length, file_size, &crc)) {
        return PKP_E_IO;
    }
    if(crc != want_crc) return PKP_E_CHECKSUM;
    if(r->sec[PKP_SEC_OBJECTS].count != nobj || r->sec[PKP_SEC_META].count < PKP_META_COUNT) {
        return PKP_E_SECTION;
    }
    r->size = file_size;
    return PKP_OK;
}

PkpStatus pkp_verify_strings(const PkpReader* r) {
    const PkpSection* s = &r->sec[PKP_SEC_STRINGS];
    if(r->size == 0) return PKP_E_RANGE;
    uint32_t crc = 0;
    if(!crc_range(r->read, r->ctx, s->offset, s->offset + s->length, &crc)) return PKP_E_IO;
    return crc == r->strings_crc ? PKP_OK : PKP_E_CHECKSUM;
}

uint32_t pkp_count(const PkpReader* r, PkpSectionType type) {
    return (type >= PKP_SEC_META && type <= PKP_SEC_KNOWN_LAST) ? r->sec[type].count : 0;
}

PkpStatus pkp_object(const PkpReader* r, uint32_t index, PkpObject* o) {
    uint8_t b[40];
    PkpStatus st = read_entry(r, PKP_SEC_OBJECTS, index, b);
    if(st != PKP_OK) return st;
    o->id = rdstr(b);
    o->code = rdstr(b + 6);
    o->title = rdstr(b + 12);
    o->definition = rdstr(b + 18);
    o->category = rd16(b + 24);
    o->kind = b[26];
    o->caps = b[27];
    o->items_first = rd32(b + 28);
    o->items_count = rd16(b + 32);
    o->rel_first = rd32(b + 34);
    o->rel_count = rd16(b + 38);
    if(!str_ok(r, o->id) || !str_ok(r, o->code) || !str_ok(r, o->title) ||
       !str_ok(r, o->definition)) {
        return PKP_E_STRING;
    }
    if(o->kind == 0 || o->kind > r->sec[PKP_SEC_KINDS].count ||
       o->category >= r->sec[PKP_SEC_CATEGORIES].count ||
       (uint64_t)o->items_first + o->items_count > r->sec[PKP_SEC_ITEMS].count ||
       (uint64_t)o->rel_first + o->rel_count > r->sec[PKP_SEC_RELATIONS].count) {
        return PKP_E_RANGE;
    }
    return PKP_OK;
}

PkpStatus pkp_item(const PkpReader* r, uint32_t index, PkpItem* it) {
    uint8_t b[8];
    PkpStatus st = read_entry(r, PKP_SEC_ITEMS, index, b);
    if(st != PKP_OK) return st;
    it->type = b[0];
    it->text = rdstr(b + 2);
    if(it->type < PKP_ITEM_INDICATOR || it->type > PKP_ITEM_LAST) return PKP_E_RANGE;
    return str_ok(r, it->text) ? PKP_OK : PKP_E_STRING;
}

PkpStatus pkp_relation(const PkpReader* r, uint32_t index, PkpRelation* rel) {
    uint8_t b[4];
    PkpStatus st = read_entry(r, PKP_SEC_RELATIONS, index, b);
    if(st != PKP_OK) return st;
    rel->type = b[0];
    rel->target = rd16(b + 2);
    if(rel->type == 0 || rel->type > r->sec[PKP_SEC_RELNAMES].count ||
       rel->target >= r->sec[PKP_SEC_OBJECTS].count) {
        return PKP_E_RANGE;
    }
    return PKP_OK;
}

PkpStatus pkp_category(const PkpReader* r, uint32_t index, PkpCategory* c) {
    uint8_t b[10];
    PkpStatus st = read_entry(r, PKP_SEC_CATEGORIES, index, b);
    if(st != PKP_OK) return st;
    c->name = rdstr(b);
    c->first = rd16(b + 6);
    c->count = rd16(b + 8);
    if(!str_ok(r, c->name)) return PKP_E_STRING;
    if((uint32_t)c->first + c->count > r->sec[PKP_SEC_OBJECTS].count) return PKP_E_RANGE;
    return PKP_OK;
}

PkpStatus pkp_quiz(const PkpReader* r, uint32_t index, PkpQuiz* q) {
    uint8_t b[34];
    PkpStatus st = read_entry(r, PKP_SEC_QUIZ, index, b);
    if(st != PKP_OK) return st;
    q->kind = b[0];
    q->answer = b[1];
    for(int i = 0; i < 4; i++) {
        q->options[i] = rd16(b + 2 + 2 * i);
        if(q->options[i] >= r->sec[PKP_SEC_OBJECTS].count) return PKP_E_RANGE;
    }
    q->prompt = rdstr(b + 10);
    q->context = rdstr(b + 16);
    q->explanation = rdstr(b + 22);
    q->response = rdstr(b + 28);
    if(!((q->kind == PKP_QUIZ_DEFINITION && q->answer < 4) ||
         (q->kind == PKP_QUIZ_TRAINER && q->answer < 2))) {
        return PKP_E_RANGE;
    }
    if(!str_ok(r, q->prompt) || !str_ok(r, q->context) || !str_ok(r, q->explanation) ||
       !str_ok(r, q->response)) {
        return PKP_E_STRING;
    }
    return PKP_OK;
}

static PkpStatus str_entry(const PkpReader* r, PkpSectionType type, uint32_t index, PkpStr* out) {
    uint8_t b[6];
    PkpStatus st = read_entry(r, type, index, b);
    if(st != PKP_OK) return st;
    *out = rdstr(b);
    return str_ok(r, *out) ? PKP_OK : PKP_E_STRING;
}

PkpStatus pkp_meta(const PkpReader* r, uint32_t field, PkpStr* out) {
    if(field >= PKP_META_COUNT) return PKP_E_RANGE;
    return str_entry(r, PKP_SEC_META, field, out);
}

PkpStatus pkp_string(const PkpReader* r, PkpStr s, char* buf, size_t cap) {
    if(cap == 0) return PKP_E_RANGE;
    buf[0] = '\0';
    if(r->size == 0) return PKP_E_RANGE;
    if(!str_ok(r, s)) return PKP_E_STRING;
    uint32_t n = s.len < cap - 1 ? s.len : (uint32_t)(cap - 1);
    if(n && !r->read(r->ctx, r->sec[PKP_SEC_STRINGS].offset + s.off, buf, n)) return PKP_E_IO;
    /* The encoder writes printable ASCII only; anything else is damage and must not reach the
     * fonts (an embedded NUL would also silently shorten the text). */
    for(uint32_t i = 0; i < n; i++) {
        if((unsigned char)buf[i] < 32 || (unsigned char)buf[i] > 126) buf[i] = '?';
    }
    buf[n] = '\0';
    return PKP_OK;
}

static PkpStatus name_of(
    const PkpReader* r,
    PkpSectionType type,
    uint8_t one_based,
    char* buf,
    size_t cap) {
    PkpStr s;
    if(cap) buf[0] = '\0';
    if(one_based == 0) return PKP_E_RANGE;
    PkpStatus st = str_entry(r, type, one_based - 1u, &s);
    return st == PKP_OK ? pkp_string(r, s, buf, cap) : st;
}

PkpStatus pkp_kind_name(const PkpReader* r, uint8_t kind, char* buf, size_t cap) {
    return name_of(r, PKP_SEC_KINDS, kind, buf, cap);
}

PkpStatus pkp_relation_name(const PkpReader* r, uint8_t type, char* buf, size_t cap) {
    return name_of(r, PKP_SEC_RELNAMES, type, buf, cap);
}

int32_t pkp_find(const PkpReader* r, const char* id) {
    size_t want = strlen(id);
    char buf[128];
    if(want >= sizeof(buf)) return -1;
    for(uint32_t i = 0; i < r->sec[PKP_SEC_OBJECTS].count; i++) {
        PkpObject o;
        if(pkp_object(r, i, &o) != PKP_OK || o.id.len != want) continue;
        if(pkp_string(r, o.id, buf, sizeof(buf)) == PKP_OK && strcmp(buf, id) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

const char* pkp_status_text(PkpStatus status) {
    return (unsigned)status < PKP_STATUS_COUNT ? pkp_status[status] : "unknown error";
}
