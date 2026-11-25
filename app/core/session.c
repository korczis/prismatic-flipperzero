#include "session.h"

#include <stdio.h>
#include <string.h>

#include "pocket.h"

typedef struct {
    char* out;
    size_t cap, at;
    bool full;
} Out;

static void put(Out* o, const char* s) {
    for(; *s; s++) {
        if(o->at + 1 >= o->cap) {
            o->full = true;
            return;
        }
        o->out[o->at++] = *s;
    }
}

/* A string value without escapes: quotes and backslashes become '?'. */
static void put_str(Out* o, const char* s) {
    char one[2] = {0, 0};
    put(o, "\"");
    for(; *s; s++) {
        one[0] = (*s == '"' || *s == '\\') ? '?' : *s;
        put(o, one);
    }
    put(o, "\"");
}

static char mark_letter(uint8_t m) {
    static const char letters[POCKET_STATES] = {'-', 'O', 'N', 'U', 'A'};
    return m < POCKET_STATES ? letters[m] : '-';
}

size_t session_line(
    char* out,
    size_t cap,
    const char* time,
    const char* pack,
    const SessionCand* c,
    uint8_t n) {
    Out o = {out, cap, 0, false};
    char num[32]; /* ",\"i\":" and a 32-bit number */
    put(&o, "{\"v\":1,\"t\":");
    put_str(&o, time);
    put(&o, ",\"pack\":");
    put_str(&o, pack);
    put(&o, ",\"c\":[");
    for(uint8_t k = 0; k < n && k < SESSION_MAX_CANDS; k++) {
        put(&o, k ? ",{\"id\":" : "{\"id\":");
        put_str(&o, c[k].id);
        snprintf(num, sizeof(num), ",\"i\":%u,\"m\":\"", c[k].index);
        put(&o, num);
        for(uint16_t m = 0; m < c[k].count && m < SESSION_MAX_MARKS; m++) {
            char one[2] = {mark_letter(c[k].marks[m]), 0};
            put(&o, one);
        }
        put(&o, "\"}");
    }
    put(&o, "]}\n");
    if(o.full) return 0;
    out[o.at] = '\0';
    return o.at;
}

/* The string value after `key` ("t":) starting at s, copied into dst; NULL when absent. */
static const char* field(const char* s, const char* end, const char* key, char* dst, size_t cap) {
    size_t kn = strlen(key);
    for(; s + kn < end; s++) {
        if(memcmp(s, key, kn) != 0) continue;
        s += kn;
        if(*s++ != '"') return NULL;
        size_t n = 0;
        while(s < end && *s != '"') {
            if(n + 1 < cap) dst[n++] = *s;
            s++;
        }
        dst[n] = '\0';
        return s < end ? s + 1 : NULL;
    }
    return NULL;
}

bool session_parse(const char* line, size_t len, SessionView* v) {
    const char* end = line + len;
    char marks[SESSION_MAX_MARKS + 1];
    memset(v, 0, sizeof(*v));
    if(len < 8 || memcmp(line, "{\"v\":1,", 7) != 0) return false;
    const char* s = field(line, end, "\"t\":", v->time, sizeof(v->time));
    if(!s) return false;
    while(v->cands < SESSION_MAX_CANDS) {
        uint8_t k = v->cands;
        const char* next = field(s, end, "\"id\":", v->id[k], sizeof(v->id[k]));
        if(!next) break;
        unsigned idx = 0;
        if(sscanf(next, ",\"i\":%u", &idx) != 1) return false;
        next = field(next, end, "\"m\":", marks, sizeof(marks));
        if(!next) return false;
        v->index[k] = (uint16_t)idx;
        v->total[k] = (uint16_t)strlen(marks);
        for(const char* m = marks; *m; m++)
            v->observed[k] += *m == 'O';
        v->cands++;
        s = next;
    }
    /* a whole line ends with "]}"; a torn one does not */
    return v->cands > 0 && len >= 2 && end[-2] == ']' && end[-1] == '}';
}
