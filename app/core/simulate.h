/* Crisis simulations (the Prismatic Platform's PrismaticCrisis.ScenarioEngine, ported).
 * Portable C99, no Furi, no allocation.
 *
 * A run starts with the trust the pack gives; each decision adds its option's trust delta,
 * clamped to 0..100, and counts as positive (impact positive) or negative (negative or
 * critical). After the last step the outcome is, in this order: optimal when trust >= 70 and
 * there were more positive than negative decisions; acceptable when trust >= 40 and at least
 * as many positive as negative; critical failure when trust < 20; otherwise suboptimal.
 * These are the source's rules (determine_outcome/2), not a judgement of anyone's conduct.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SIM_OPTIMAL,
    SIM_ACCEPTABLE,
    SIM_SUBOPTIMAL,
    SIM_CRITICAL_FAILURE,
} SimOutcome;

typedef struct {
    int16_t trust;
    uint8_t positive;
    uint8_t negative;
    uint8_t decisions;
} SimState;

/* One option as the pack stores it, split in place: "risk|impact|delta|type|option|consequence". */
typedef struct {
    const char* risk;
    const char* impact;
    int delta;
    const char* type;
    const char* option;
    const char* consequence;
} SimOption;

void sim_start(SimState* s, int trust);
void sim_apply(SimState* s, const char* impact, int delta);
SimOutcome sim_outcome(const SimState* s);
const char* sim_outcome_name(SimOutcome o);
/* False when the text does not have all six fields. */
bool sim_option_split(char* text, SimOption* out);
