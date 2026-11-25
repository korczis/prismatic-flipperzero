/* Saved detector sessions: one line of JSON per session, appended to a bounded text file that is
 * its own export (JSON Lines, readable on any computer). Portable C99, no Furi, no allocation.
 *
 *   {"v":1,"t":"2026-11-25T12:00:00","pack":"kompendium 1","c":[{"id":"...","i":173,"m":"OONUA"}]}
 *
 * `t` is the device clock's local time when the reader pressed Save; `c` holds one or two
 * candidates: the object's stable id, its index in the pack the marks were made with, and the
 * marks in order, one letter each: O observed, N not observed, U unknown, A not applicable,
 * - unanswered. A session is saved only when the reader asks; nothing is stored by default.
 * A torn last line (power lost mid-append) does not parse and is skipped.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SESSION_MAX_CANDS 2u
#define SESSION_MAX_MARKS 64u
#define SESSION_MAX_LINE  400u
#define SESSION_MAX_FILE  32768u /* about a hundred sessions; Save refuses beyond it */

typedef struct {
    const char* id; /* stable object id */
    uint16_t index; /* its index in the pack */
    const uint8_t* marks; /* PocketAnswer values */
    uint16_t count;
} SessionCand;

typedef struct {
    char time[20];
    uint8_t cands;
    char id[SESSION_MAX_CANDS][96];
    uint16_t index[SESSION_MAX_CANDS];
    uint16_t observed[SESSION_MAX_CANDS];
    uint16_t total[SESSION_MAX_CANDS];
} SessionView;

/* One session as a line (with its newline) in `out`; 0 when it does not fit `cap`. Quotes and
 * backslashes in the strings become '?', so the line is valid JSON without escapes. */
size_t session_line(
    char* out,
    size_t cap,
    const char* time,
    const char* pack,
    const SessionCand* cands,
    uint8_t n);
/* Parse one line written by session_line (no newline needed); false for anything else. */
bool session_parse(const char* line, size_t len, SessionView* out);
