/* pe_preflop_deal_sampler.h - sampled correlated private deals (Lane B). */

#ifndef POKER_EVAL_PE_PREFLOP_DEAL_SAMPLER_H
#define POKER_EVAL_PE_PREFLOP_DEAL_SAMPLER_H

#include <poker_eval/core/pcg_rng.h>
#include <poker_eval/solver/pe_holdem_deals.h>
#include <poker_eval/solver/pe_omaha_deals.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_PREFLOP_MAX_PLAYERS 8u

typedef enum
{
    PE_PREFLOP_HOLDEM = 0,
    PE_PREFLOP_PLO4,
    PE_PREFLOP_PLO5,
    PE_PREFLOP_PLO6
} pe_preflop_variant_t;

typedef struct
{
    pe_preflop_variant_t variant;
    mask_t board;
    uint8_t player_count;
    uint8_t hole_cards;
    /* Borrowed pe_holdem_range_t[] or pe_omaha_range_t[].  NULL means every
       player holds the complete range (see complete_ranges). */
    const void *ranges;

    /* Set when `ranges` is NULL: every player's range is "any hand", so the
       deal is drawn straight from the live deck instead of walking a combo
       list.  This is not an optimisation detail, it is what makes 5- and
       6-card Omaha solvable at all: a full PLO6 range is C(52,6) = 20.4M
       combos (326 MB per player) and the sequential proposal is linear in
       that count per drawn deal.  Drawing uniformly from the live deck is
       mathematically the same proposal -- a uniform n-subset, with the
       per-player legal total C(live, n) -- at O(hole_cards) per player.
       Only the all-players-complete case is modelled, because a mixed deal
       would need per-choice completion checks that have no closed form. */
    uint8_t complete_ranges;

    /* Exact normalisation of the product range distribution.  When zero,
       sampling still applies target/proposal importance weighting, but the
       returned value is scaled for the unnormalised product range.  Call
       pe_preflop_deal_sampler_measure() when a normalised weight is needed
       and the range space is small enough to enumerate. */
    double reference_weight_sum;
} pe_preflop_deal_sampler_t;

typedef struct
{
    mask_t holes[PE_PREFLOP_MAX_PLAYERS];
    double target_weight;
    double proposal_probability;
    double importance_ratio;
} pe_preflop_deal_sample_t;

int pe_preflop_deal_sampler_init_holdem(
    pe_preflop_deal_sampler_t *out, mask_t board,
    const pe_holdem_range_t *ranges, uint8_t player_count);

int pe_preflop_deal_sampler_init_omaha(
    pe_preflop_deal_sampler_t *out, mask_t board,
    const pe_omaha_range_t *ranges, uint8_t player_count,
    uint8_t hole_cards);

/* Exact product-range normalisation and legal joint-deal count. */
int pe_preflop_deal_sampler_measure(const pe_preflop_deal_sampler_t *sampler,
                                    size_t *out_deal_count,
                                    double *out_weight_sum);

/* Draw one correlated private deal. Returns zero on success. */
int pe_preflop_deal_sampler_sample(const pe_preflop_deal_sampler_t *sampler,
                                   pe_rng_t *rng,
                                   pe_preflop_deal_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PREFLOP_DEAL_SAMPLER_H */
