/* Find by name: prefix search over the pack's search index (sections 10 and 11). Portable C99,
 * no allocation: one PkpSearch (about 1.2 KiB) lives inside the app.
 *
 * A query is typed a letter at a time. The keys of a prefix are one contiguous range of the
 * sorted key table and their postings one contiguous run, so each letter costs two lower-bound
 * searches inside the previous range and one pass over that run (64-byte chunks on the stack).
 * The letters that can follow the query are found by lower bounds as well and kept as a mask:
 * the wheel offers only those, so a query never reaches zero matches, and turning the wheel
 * reads nothing. Matches are kept as two bitsets over the objects (any key, a strong key) and
 * listed strong first, each group in pack (Library) order. tools/pocket.py search() is the
 * reference the differential test compares this with.
 */
#pragma once

#include "pkp.h"

#define PKP_SEARCH_MAX_OBJECTS  4096u /* a larger pack has no search on the device */
/* The wheel's order: a mask bit is a letter's position here. */
#define PKP_SEARCH_LETTERS      "abcdefghijklmnopqrstuvwxyz0123456789"
#define PKP_SEARCH_LETTER_COUNT 36u

typedef struct {
    const PkpReader* r;
    uint16_t keys;
    uint16_t objects; /* the bound of every object index, fixed at init: at most 4096 */
    uint8_t len;
    char query[PKP_SEARCH_KEY_LEN + 1];
    /* per prefix length: the range of keys that start with the prefix, and the letters that can
     * follow it; Left pops back to the previous level without searching again */
    uint16_t lo[PKP_SEARCH_KEY_LEN + 1];
    uint16_t hi[PKP_SEARCH_KEY_LEN + 1];
    uint64_t next[PKP_SEARCH_KEY_LEN + 1];
    uint16_t count; /* objects that match the query */
    uint16_t strong; /* of which strong: the first word of the title, or the code */
    uint8_t any[PKP_SEARCH_MAX_OBJECTS / 8];
    uint8_t strong_bits[PKP_SEARCH_MAX_OBJECTS / 8];
} PkpSearch;

/* An empty query over the pack: PKP_E_MISSING when it has no (or an empty) index, PKP_E_RANGE
 * when it is too large for the bitsets. The engine relies on what pkp_verify_search() proves
 * (sorted keys, contiguous runs), so init streams both tables through it first and refuses an
 * index it rejects with its status; then it reads the letters a query can start with. A card
 * changed afterwards can still only yield an error, never a read or write out of bounds. */
PkpStatus pkp_search_init(PkpSearch* s, const PkpReader* r);
/* Appends one letter. PKP_E_RANGE for a letter the wheel does not offer; any read or index
 * error leaves the query empty (and says why). */
PkpStatus pkp_search_push(PkpSearch* s, char letter);
/* Removes the last letter (the matches of the shorter query are read again). */
PkpStatus pkp_search_pop(PkpSearch* s);
/* Back to the empty query; reads nothing. */
void pkp_search_clear(PkpSearch* s);
/* The letters that can follow the query, a bit per position in PKP_SEARCH_LETTERS. */
uint64_t pkp_search_next(const PkpSearch* s);
/* Up to `cap` matches from match number `from` on, strong first; returns how many. */
uint32_t pkp_search_results(const PkpSearch* s, uint32_t from, uint16_t* out, uint32_t cap);
/* The letter at a wheel position, and a letter's position (-1 outside the alphabet). */
char pkp_search_letter(uint32_t position);
int pkp_search_position(char letter);
