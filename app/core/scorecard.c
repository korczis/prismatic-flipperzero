#include "scorecard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Split `text` at '|' into exactly `n` fields. */
static bool split(char* text, char** field, int n) {
    field[0] = text;
    for(int k = 1; k < n; k++) {
        char* bar = strchr(field[k - 1], '|');
        if(!bar) return false;
        *bar = '\0';
        field[k] = bar + 1;
    }
    return true;
}

bool score_setup(const char* text, int* base, int* min, int* max) {
    return sscanf(text, "%d|%d|%d", base, min, max) == 3 && *min <= *max;
}

bool score_choice(char* text, ScoreChoice* out) {
    char* f[3];
    if(!split(text, f, 3)) return false;
    out->weight = atoi(f[0]);
    out->label = f[1];
    out->note = f[2];
    return true;
}

bool score_band(char* text, ScoreBand* out) {
    char* f[4];
    if(!split(text, f, 4)) return false;
    out->top = atoi(f[0]);
    out->code = f[1];
    out->label = f[2];
    out->meaning = f[3];
    return true;
}

int score_clamp(int value, int min, int max) {
    return value < min ? min : value > max ? max : value;
}
