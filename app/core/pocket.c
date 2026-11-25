#include "pocket.h"

#include <stdio.h>

static const char* const pocket_names[POCKET_STATES] = {
    [POCKET_UNANSWERED] = "unanswered",
    [POCKET_OBSERVED] = "observed",
    [POCKET_NOT_OBSERVED] = "not observed",
    [POCKET_UNKNOWN] = "unknown",
    [POCKET_NOT_APPLICABLE] = "not applicable",
};

void pocket_summarize(const uint8_t* answers, size_t n, PocketSummary* out) {
    for(int i = 0; i < POCKET_STATES; i++) out->counts[i] = 0;
    out->total = 0;
    for(size_t i = 0; i < n; i++) {
        uint8_t a = answers[i] < POCKET_STATES ? answers[i] : POCKET_UNANSWERED;
        out->counts[a]++;
        out->total++;
    }
}

size_t pocket_summary_text(const PocketSummary* s, char* buf, size_t cap) {
    /* Each line is formatted only while it fits; a short buffer ends cleanly truncated. */
    if(cap == 0) return 0;
    size_t n = (size_t)snprintf(
        buf, cap, "Observed %u of %u markers", s->counts[POCKET_OBSERVED], (unsigned)s->total);
    static const uint8_t order[] = {
        POCKET_NOT_OBSERVED, POCKET_UNKNOWN, POCKET_NOT_APPLICABLE, POCKET_UNANSWERED};
    for(size_t k = 0; k < sizeof(order) && n < cap; k++) {
        if(s->counts[order[k]]) {
            n += (size_t)snprintf(
                buf + n, cap - n, "\n%s: %u", pocket_names[order[k]], s->counts[order[k]]);
        }
    }
    return n < cap ? n : cap - 1;
}

const char* pocket_answer_name(uint8_t answer) {
    return answer < POCKET_STATES ? pocket_names[answer] : "invalid";
}

int pocket_quiz_correct(const PkpQuiz* q, uint8_t choice) {
    return choice < 4 && choice == q->answer;
}
