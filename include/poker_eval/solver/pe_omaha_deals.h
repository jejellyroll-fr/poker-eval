/* pe_omaha_deals.h - correlated Omaha 4/5/6-card private deals */

#ifndef POKER_EVAL_PE_OMAHA_DEALS_H
#define POKER_EVAL_PE_OMAHA_DEALS_H

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
} pe_omaha_combo_t;

typedef struct
{
    const pe_omaha_combo_t *combos;
    size_t count;
} pe_omaha_range_t;

typedef int (*pe_omaha_deal_callback)(const mask_t *holes,
                                      uint8_t player_count,
                                      double weight,
                                      void *user);

size_t pe_omaha_combo_count(mask_t dead_cards, uint8_t hole_cards);

int pe_omaha_enumerate_combos(mask_t dead_cards, uint8_t hole_cards,
                              mask_t *out, size_t capacity,
                              size_t *out_count);

int pe_omaha_deals_measure(mask_t board, const pe_omaha_range_t *ranges,
                           uint8_t player_count, uint8_t hole_cards,
                           size_t *deal_count, double *weight_sum);

int pe_omaha_deals_enumerate(mask_t board, const pe_omaha_range_t *ranges,
                             uint8_t player_count, uint8_t hole_cards,
                             pe_omaha_deal_callback callback, void *user,
                             size_t *deal_count, double *weight_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_OMAHA_DEALS_H */
