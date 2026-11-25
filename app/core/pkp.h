/* Prismatic Knowledge Pack (PKP1) reader. Portable C99: no Furi, no allocation.
 *
 * A pack is untrusted input. pkp_open() checks the header, every section's bounds and the
 * index CRC-32 (everything after the header except the string pool); each accessor then checks
 * the one record it reads (string bounds, index ranges, enum ranges against the pack's own
 * tables). The string pool, most of the file, has its own CRC that pkp_verify_strings()
 * checks on request: a corrupted string can only show wrong text, never read out of bounds.
 * Nothing is cast from file bytes to a struct. A checksum detects corruption; it
 * authenticates nothing.
 *
 * The layout is defined by tools/pocket.py (encode/decode); docs/pocket.md describes it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PKP_FORMAT_VERSION 1
#define PKP_MAX_STRING     2048u
#define PKP_HEADER_SIZE    32u
#define PKP_SECTION_SIZE   16u
#define PKP_MAX_SECTIONS   32u

typedef enum {
    PKP_OK = 0,
    PKP_E_IO,
    PKP_E_TRUNCATED,
    PKP_E_MAGIC,
    PKP_E_VERSION,
    PKP_E_FEATURES,
    PKP_E_SIZE,
    PKP_E_CHECKSUM,
    PKP_E_DIRECTORY,
    PKP_E_SECTION,
    PKP_E_MISSING,
    PKP_E_RANGE,
    PKP_E_STRING,
    PKP_STATUS_COUNT,
} PkpStatus;

typedef enum {
    PKP_SEC_META = 1,
    PKP_SEC_STRINGS,
    PKP_SEC_CATEGORIES,
    PKP_SEC_OBJECTS,
    PKP_SEC_ITEMS,
    PKP_SEC_RELATIONS,
    PKP_SEC_QUIZ,
    PKP_SEC_KINDS,
    PKP_SEC_RELNAMES,
    PKP_SEC_KNOWN_LAST = PKP_SEC_RELNAMES,
} PkpSectionType;

typedef enum {
    PKP_ITEM_INDICATOR = 1,
    PKP_ITEM_ACTION,
    PKP_ITEM_ESCALATION,
    PKP_ITEM_ALTERNATIVE,
    PKP_ITEM_VALIDATION,
    PKP_ITEM_HEADING,
    PKP_ITEM_TEXT,
    PKP_ITEM_EXAMPLE,
    PKP_ITEM_AXIS, /* a scale of an assessment: its name */
    PKP_ITEM_LEVEL, /* one grade of the scale above: "code|label|meaning" */
    PKP_ITEM_SIM_START, /* a simulation's starting trust, decimal */
    PKP_ITEM_SIM_STEP, /* one step's prompt; its options follow */
    PKP_ITEM_SIM_OPTION, /* "risk|impact|trust delta|consequence type|option|consequence" */
    PKP_ITEM_SCORE_SETUP, /* a scorecard's "base|min|max" */
    PKP_ITEM_SCORE_QUESTION, /* one question; its choices follow */
    PKP_ITEM_SCORE_CHOICE, /* "weight|label|note" */
    PKP_ITEM_SCORE_BAND, /* "max|code|label|meaning", ascending */
    PKP_ITEM_CALC, /* a calculator: "algorithm|prior|confirm|reject|c1,c2,..." in percent */
    PKP_ITEM_LAST = PKP_ITEM_CALC,
} PkpItemType;

enum {
    PKP_CAP_BROWSE = 1,
    PKP_CAP_DETECT = 2,
    PKP_CAP_APPLY = 4,
    PKP_CAP_QUIZ = 8,
    PKP_CAP_COMPARE = 16,
    PKP_CAP_ASSESS = 32,
    PKP_CAP_SIMULATE = 64,
    PKP_CAP_SCORE = 128,
};

enum {
    PKP_QUIZ_DEFINITION = 1, /* options[answer] is the object whose definition is the prompt */
    PKP_QUIZ_TRAINER = 2, /* prompt is an utterance; answer 0 manipulative, 1 benign */
};

enum {
    PKP_META_PACK_ID,
    PKP_META_TITLE,
    PKP_META_VERSION,
    PKP_META_SOURCE_REPOSITORY,
    PKP_META_SOURCE_COMMIT,
    PKP_META_GENERATOR,
    PKP_META_LICENCE,
    PKP_META_COUNT,
};

/* Reads exactly len bytes at offset into buf; false on any I/O failure. */
typedef bool (*PkpReadFn)(void* ctx, uint32_t offset, void* buf, uint32_t len);

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t count;
} PkpSection;

typedef struct {
    PkpReadFn read;
    void* ctx;
    uint32_t size;
    uint32_t strings_crc;
    uint32_t present; /* bit per known section type seen in the directory */
    PkpSection sec[PKP_SEC_KNOWN_LAST + 1];
} PkpReader;

typedef struct {
    uint32_t off;
    uint16_t len;
} PkpStr;

typedef struct {
    PkpStr id, code, title, definition;
    uint16_t category;
    uint8_t kind;
    uint8_t caps;
    uint32_t items_first;
    uint16_t items_count;
    uint32_t rel_first;
    uint16_t rel_count;
} PkpObject;

typedef struct {
    uint8_t type;
    PkpStr text;
} PkpItem;

typedef struct {
    uint8_t type;
    uint16_t target;
} PkpRelation;

typedef struct {
    PkpStr name;
    uint16_t first;
    uint16_t count;
} PkpCategory;

typedef struct {
    uint8_t kind;
    uint8_t answer;
    uint16_t options[4];
    PkpStr prompt, context, explanation, response;
} PkpQuiz;

PkpStatus pkp_open(PkpReader* r, PkpReadFn read, void* ctx, uint32_t file_size);
/* Streams the string pool through CRC-32 and compares it with the header. */
PkpStatus pkp_verify_strings(const PkpReader* r);
uint32_t pkp_count(const PkpReader* r, PkpSectionType type);
PkpStatus pkp_object(const PkpReader* r, uint32_t index, PkpObject* out);
PkpStatus pkp_item(const PkpReader* r, uint32_t index, PkpItem* out);
PkpStatus pkp_relation(const PkpReader* r, uint32_t index, PkpRelation* out);
PkpStatus pkp_category(const PkpReader* r, uint32_t index, PkpCategory* out);
PkpStatus pkp_quiz(const PkpReader* r, uint32_t index, PkpQuiz* out);
PkpStatus pkp_meta(const PkpReader* r, uint32_t field, PkpStr* out);
/* Kind (1-based) and relation type (1-based) names come from the pack's own tables. */
PkpStatus pkp_kind_name(const PkpReader* r, uint8_t kind, char* buf, size_t cap);
PkpStatus pkp_relation_name(const PkpReader* r, uint8_t type, char* buf, size_t cap);
/* Copies the string into buf (always NUL-terminated); a short buffer truncates safely. */
PkpStatus pkp_string(const PkpReader* r, PkpStr s, char* buf, size_t cap);
/* Index of the object whose id equals `id`, or -1. Linear and bounded by the object count. */
int32_t pkp_find(const PkpReader* r, const char* id);

const char* pkp_status_text(PkpStatus status);
uint32_t pkp_crc32(uint32_t crc, const uint8_t* data, size_t len);
