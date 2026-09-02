/* pe_omaha_river.h - exact high-only PLO4/PLO5/PLO6 river terminal */

#ifndef POKER_EVAL_PE_OMAHA_RIVER_H
#define POKER_EVAL_PE_OMAHA_RIVER_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_omaha_deals.h>

#ifdef __cplusplus
extern "C" {
#endif

int pe_omaha_river_range_values(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_OMAHA_RIVER_H */
