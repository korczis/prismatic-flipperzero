#include "bayes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double bayes_update(double p, double c) {
    double den = c * p + (1.0 - c) * (1.0 - p);
    return den > 0.0 ? c * p / den : p;
}

double bayes_fuse(double prior, const uint8_t* percents, uint8_t n) {
    double p = prior;
    for(uint8_t k = 0; k < n; k++)
        p = bayes_update(p, percents[k] / 100.0);
    return p;
}

const char* bayes_status(double percent, uint8_t confirm, uint8_t reject) {
    if(percent >= confirm) return "confirmed";
    if(percent <= reject) return "rejected";
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
