/* Observation-driven detection and quiz checks. Portable C99, no Furi, no allocation.
 *
 * The detector only counts what the operator marked. It never turns the counts into a score
 * or a probability: an indicator that was not observed is not evidence against a pattern, and
 * an unknown stays unknown. The result names candidate patterns and the next verification
 * steps; it says nothing about anyone's intent or state of mind.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pkp.h"

typedef enum {
    POCKET_UNANSWERED = 0,
    POCKET_OBSERVED,
    POCKET_NOT_OBSERVED,
    POCKET_UNKNOWN,
    POCKET_NOT_APPLICABLE,
    POCKET_STATES,
} PocketAnswer;

typedef struct {
    uint16_t counts[POCKET_STATES];
    uint16_t total;
} PocketSummary;

void pocket_summarize(const uint8_t* answers, size_t n, PocketSummary* out);
/* "Observed 3/6" plus the other non-zero states, one per line; returns bytes written. */
size_t pocket_summary_text(const PocketSummary* s, char* buf, size_t cap);
const char* pocket_answer_name(uint8_t answer);
/* True when `choice` (0..3) is the quiz's answer position. */
int pocket_quiz_correct(const PkpQuiz* q, uint8_t choice);
