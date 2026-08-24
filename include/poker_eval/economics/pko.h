/* Explicit PKO bounty overlay on top of a tournament equity model. */
#ifndef POKER_EVAL_ECONOMICS_PKO_H
#define POKER_EVAL_ECONOMICS_PKO_H

#include <poker_eval/economics/icm.h>
#include <poker_eval/solver/pe_range.h>

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

typedef struct {
    uint16_t combo_index[ICM_MAX_PLAYERS];
    StdDeck_CardMask hand[ICM_MAX_PLAYERS];
    double weight;
} pe_pko_range_profile_t;

/* Return pairwise elimination probabilities for one all-in/tournament
 * outcome. `out_probability[winner][victim]` is allowed to be fractional so
 * split pots and tie-aware evaluators can be plugged in without changing the
 * range enumerator. */
typedef int (*pe_pko_range_outcome_fn)(const pe_pko_range_profile_t *profile,
                                       int num_players,
                                       double out_probability[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS],
                                       void *user_data);

typedef struct {
    pe_pko_input_t base;
    pe_range_view_t ranges[ICM_MAX_PLAYERS];
    size_t max_profiles;
    pe_pko_range_outcome_fn outcome;
    void *user_data;
} pe_pko_range_input_t;

typedef struct {
    pe_pko_result_t pko;
    double elimination_probability[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS];
    size_t profile_count;
    double valid_profile_probability;
} pe_pko_range_result_t;

/* Enumerate independent private ranges with exact card removal, derive the
 * elimination matrix from the supplied showdown/tournament outcome callback,
 * then run the regular PKO + ICM calculation. */
int pe_pko_calculate_from_ranges(const pe_pko_range_input_t *input,
                                 pe_pko_range_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_PKO_H */
