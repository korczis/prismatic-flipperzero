/* Quiz progress for spaced repetition (Leitner boxes). Portable C99, no Furi, no allocation.
 *
 * Each answered question is one entry: a stable key (FNV-1a of the question's prompt text, so it
 * survives a rebuilt pack whose question moved), the question's index in the pack it was last
 * seen in, its box (1..5), how often it was answered and whether the last answer was right, and
 * the session from which it is due again. A right answer moves a question up one box, a wrong
 * one back to box 1; box n comes back after 2^(n-1) sessions. A session is one start of the app.
 *
 * The file is versioned, bounded and checksummed: "PKPS", version, generation, session, count,
 * entries, CRC-32. Writers keep two files (the progress and a temporary copy); the reader takes
 * whichever valid one has the higher generation, so an interrupted write or a rename that never
 * happened loses at most the last answer, never the file. The data is the reader's own quiz
 * results, nothing about anyone else; Info offers to forget it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROGRESS_CAP      256u
#define PROGRESS_BOXES    5u
#define PROGRESS_ENTRY    12u
#define PROGRESS_HEADER   20u
#define PROGRESS_MAX_FILE (PROGRESS_HEADER + PROGRESS_CAP * PROGRESS_ENTRY + 4u)

typedef struct {
    uint32_t key;
    uint16_t quiz; /* index in the pack it was last seen in */
    uint8_t box; /* 1..PROGRESS_BOXES */
    uint8_t last_right;
    uint16_t attempts;
    uint16_t due; /* the session from which it is due again */
} ProgressEntry;

typedef struct {
    uint32_t generation; /* bumped by every save */
    uint16_t session; /* bumped by every start */
    uint16_t count;
    ProgressEntry entries[PROGRESS_CAP];
} Progress;

typedef enum {
    PROGRESS_OK = 0,
    PROGRESS_E_SHORT, /* shorter than its header or its count says */
    PROGRESS_E_MAGIC,
    PROGRESS_E_VERSION,
    PROGRESS_E_COUNT, /* more entries than the cap */
    PROGRESS_E_CRC,
} ProgressStatus;

/* FNV-1a, 32 bits: the stable key of a question's prompt. */
uint32_t progress_key(const char* text);
void progress_init(Progress* p);
/* Decode one file image; on any error `p` is left empty. */
ProgressStatus progress_decode(Progress* p, const uint8_t* data, size_t len);
/* The better of two file images (either may be NULL or invalid): the valid one with the higher
 * generation; empty when neither is valid. Returns which was taken: 0 none, 1 a, 2 b. */
int progress_load(Progress* p, const uint8_t* a, size_t alen, const uint8_t* b, size_t blen);
/* Encode into `out` (PROGRESS_MAX_FILE bytes suffice); bumps the generation. Returns bytes. */
size_t progress_encode(Progress* p, uint8_t* out, size_t cap);
/* Record an answer. A full table drops the entry answered longest ago. */
void progress_record(Progress* p, uint32_t key, uint16_t quiz, bool right);
/* Entries due in the current session. */
uint16_t progress_due_count(const Progress* p);
/* The due entry in the lowest box (then the longest overdue); -1 when none is due. */
int progress_next_due(const Progress* p);
/* Forget one entry (a question no longer in the pack). */
void progress_drop(Progress* p, int index);
