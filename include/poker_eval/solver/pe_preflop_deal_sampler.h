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
    const void *ranges; /* borrowed pe_holdem_range_t[] or pe_omaha_range_t[] */

    /* Exact normalisation of the product range distribution.  Zero selects
       the sequential card-removal proposal as the reference distribution.
       Call pe_preflop_deal_sampler_measure() when exact importance weights
       are required and the range space is small enough to enumerate. */
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
