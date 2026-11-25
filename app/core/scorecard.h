/* Scorecards (the Prismatic Platform's deterministic scoring rules, as data). Portable C99, no
 * Furi, no allocation. A score is the base plus the weights of the chosen answers, clamped to
 * min..max; its band is the first whose top it does not exceed. The pack stores each piece as a
 * '|'-separated text; these helpers split one in place.
 */
#pragma once

#include <stdbool.h>

typedef struct {
    int weight;
    const char* label;
    const char* note; /* repeated in the result when chosen; may be empty */
} ScoreChoice;

typedef struct {
    int top;
    const char* code;
    const char* label;
    const char* meaning;
} ScoreBand;

/* "base|min|max"; false when malformed. */
bool score_setup(const char* text, int* base, int* min, int* max);
bool score_choice(char* text, ScoreChoice* out);
bool score_band(char* text, ScoreBand* out);
int score_clamp(int value, int min, int max);
