#include "simulate.h"

#include <stdlib.h>
#include <string.h>

void sim_start(SimState* s, int trust) {
    memset(s, 0, sizeof(*s));
    s->trust = (int16_t)(trust < 0 ? 0 : trust > 100 ? 100 : trust);
}

void sim_apply(SimState* s, const char* impact, int delta) {
    int t = s->trust + delta;
    s->trust = (int16_t)(t < 0 ? 0 : t > 100 ? 100 : t);
    s->positive += strcmp(impact, "positive") == 0;
    s->negative += strcmp(impact, "negative") == 0 || strcmp(impact, "critical") == 0;
    s->decisions++;
}

SimOutcome sim_outcome(const SimState* s) {
    if(s->trust >= 70 && s->positive > s->negative) return SIM_OPTIMAL;
    if(s->trust >= 40 && s->positive >= s->negative) return SIM_ACCEPTABLE;
    if(s->trust < 20) return SIM_CRITICAL_FAILURE;
    return SIM_SUBOPTIMAL;
}

const char* sim_outcome_name(SimOutcome o) {
    static const char* const names[] = {"Optimal", "Acceptable", "Suboptimal", "Critical failure"};
    return names[o];
}

bool sim_option_split(char* text, SimOption* out) {
    char* field[6];
    field[0] = text;
    for(int k = 1; k < 6; k++) {
        char* bar = strchr(field[k - 1], '|');
        if(!bar) return false;
        *bar = '\0';
        field[k] = bar + 1;
    }
    out->risk = field[0];
    out->impact = field[1];
    out->delta = atoi(field[2]);
    out->type = field[3];
    out->option = field[4];
    out->consequence = field[5];
    return true;
}
