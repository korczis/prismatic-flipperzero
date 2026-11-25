#include "bayes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float bayes_update(float p, float c) {
    float den = c * p + (1.0f - c) * (1.0f - p);
    return den > 0.0f ? c * p / den : p;
}

float bayes_fuse(float prior, const uint8_t* percents, uint8_t n) {
    float p = prior;
    for(uint8_t k = 0; k < n; k++)
        p = bayes_update(p, (float)percents[k] / 100.0f);
    return p;
}

const char* bayes_status(float percent, uint8_t confirm, uint8_t reject) {
    if(percent >= (float)confirm) return "confirmed";
    if(percent <= (float)reject) return "rejected";
    return "tentative";
}

bool calc_parse(const char* text, CalcSpec* out) {
    unsigned prior, confirm, reject;
    int used = 0;
    memset(out, 0, sizeof(*out));
    if(sscanf(text, "%11[^|]|%u|%u|%u|%n", out->algorithm, &prior, &confirm, &reject, &used) < 4 ||
       used == 0 || prior > 100 || confirm > 100 || reject > 100)
        return false;
    out->prior = (uint8_t)prior;
    out->confirm = (uint8_t)confirm;
    out->reject = (uint8_t)reject;
    for(const char* s = text + used; *s && out->n_choices < CALC_MAX_CHOICES;) {
        char* end;
        long v = strtol(s, &end, 10);
        if(end == s || v < 1 || v > 99) return false;
        out->choices[out->n_choices++] = (uint8_t)v;
        s = *end == ',' ? end + 1 : end;
    }
    return out->n_choices > 0;
}
