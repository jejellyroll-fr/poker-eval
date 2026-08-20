#ifndef POKER_EVAL_CFR_BET_SIZING_H
#define POKER_EVAL_CFR_BET_SIZING_H

#include <poker_eval/core/pokereval_export.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_BET_SIZING_MAX_OPTIONS 16

typedef struct
{
    double bet_size_fraction;
    double bet_amount;
    double expected_value;
    double frequency;
} pe_bet_size_option_t;

typedef struct
{
    pe_bet_size_option_t sizes[PE_BET_SIZING_MAX_OPTIONS];
    int num_sizes;
    int optimal_index;
    double max_ev;
} pe_bet_sizing_result_t;

/* Evaluate one candidate. The fraction is relative to the pot and the amount
 * is capped at the effective stack, so a candidate can represent an all-in. */
typedef double (*pe_bet_size_ev_fn)(double bet_size_fraction,
                                    double bet_amount,
                                    void *ctx);

/* Find the best candidate and build a mixed strategy over tied candidates.
 * Candidates must be finite and strictly positive. Fractions that map to the
 * same capped amount are consolidated into one canonical action. Ties use a
 * relative tolerance so small floating-point noise does not remove a mix. */
POKEREVAL_EXPORT int pe_optimal_bet_size(
    double pot,
    double effective_stack,
    const double *candidate_fractions,
    int num_candidates,
    pe_bet_size_ev_fn value_fn,
    void *ctx,
    pe_bet_sizing_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
