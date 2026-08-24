/* Explicit PKO bounty overlay on top of a tournament equity model. */
#ifndef POKER_EVAL_ECONOMICS_PKO_H
#define POKER_EVAL_ECONOMICS_PKO_H

#include <poker_eval/economics/icm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    icm_asymmetric_input_t icm;
    double bounties[ICM_MAX_PLAYERS];
    /* Probability that player i receives player j's bounty in the future. */
    double elimination_probability[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];
    double bounty_multiplier;
} pe_pko_input_t;

typedef struct {
    double icm_ev[ICM_MAX_PLAYERS];
    double bounty_ev[ICM_MAX_PLAYERS];
    double total_ev[ICM_MAX_PLAYERS];
} pe_pko_result_t;

/* Combine exact player-specific ICM rewards with an explicit bounty capture
 * matrix. The caller supplies the future knockout probabilities; this keeps
 * tournament dynamics separate from the exact payout calculation. */
int pe_pko_calculate(const pe_pko_input_t *input, pe_pko_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_PKO_H */
