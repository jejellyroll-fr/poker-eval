/*
 * pe_holdem_deals.h - exact correlated private Hold'em deals
 *
 * A range is a weighted list of two-card combinations.  The deal iterator
 * joins those lists while applying card removal between every player.  It is
 * deliberately independent of a tree/vector so it can be used by chance
 * nodes, terminal evaluators and import adapters alike.
 */

#ifndef POKER_EVAL_PE_HOLDEM_DEALS_H
#define POKER_EVAL_PE_HOLDEM_DEALS_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/modern_cardmask.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    mask_t cards;
    double weight;
} pe_holdem_combo_t;

typedef struct
{
    const pe_holdem_combo_t *combos;
    size_t count;
} pe_holdem_range_t;

typedef int (*pe_holdem_deal_callback)(const mask_t *holes,
                                       uint8_t player_count,
                                       double weight,
                                       void *user);

/* Number of legal two-card combinations after removing dead cards. */
size_t pe_holdem_combo_count(mask_t dead_cards);

/* Enumerate every legal two-card combination in deterministic card order. */
int pe_holdem_enumerate_combos(mask_t dead_cards, mask_t *out,
                               size_t capacity, size_t *out_count);

/*
 * Measure the joint range.  Only positive finite combo weights contribute.
 * `weight_sum` is the exact unnormalised mass after card removal; callers can
 * normalise each callback weight by this value.
 */
int pe_holdem_deals_measure(mask_t board, const pe_holdem_range_t *ranges,
                            uint8_t player_count, size_t *deal_count,
                            double *weight_sum);

/*
 * Visit each legal joint deal once.  Callback weights are unnormalised range
 * products; divide by pe_holdem_deals_measure(..., weight_sum) for an exact
 * correlated-deal probability.
 */
int pe_holdem_deals_enumerate(mask_t board, const pe_holdem_range_t *ranges,
                              uint8_t player_count,
                              pe_holdem_deal_callback callback, void *user,
                              size_t *deal_count, double *weight_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HOLDEM_DEALS_H */
