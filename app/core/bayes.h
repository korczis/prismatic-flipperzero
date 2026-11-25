/* Bayesian fusion of independent sources (the Prismatic Platform's
 * CorrelationEngine.bayesian_fusion/1, ported). Portable C99, no Furi, no allocation.
 *
 * Each source's confidence c updates the belief p to c*p / (c*p + (1-c)(1-p)); a zero
 * denominator leaves p unchanged. Below 0.5 a source lowers the belief. The status follows the
 * hypothesis lifecycle: confirmed at `confirm` or above, rejected at `reject` or below.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CALC_MAX_CHOICES 8u
#define CALC_MAX_SOURCES 12u

typedef struct {
    char algorithm[12];
    uint8_t prior, confirm, reject; /* percent */
    uint8_t choices[CALC_MAX_CHOICES];
    uint8_t n_choices;
} CalcSpec;

double bayes_update(double p, double c);
/* The belief after every source in order, from `prior`, all in 0..1. */
double bayes_fuse(double prior, const uint8_t* percents, uint8_t n);
/* "confirmed", "rejected" or "tentative" for a belief in percent. */
const char* bayes_status(double percent, uint8_t confirm, uint8_t reject);
/* Parse "algorithm|prior|confirm|reject|c1,c2,..."; false when malformed. */
bool calc_parse(const char* text, CalcSpec* out);
