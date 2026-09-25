/* pe_omaha_river.h - exact PLO4/PLO5/PLO6 river terminal
 *
 * Two payoff models share the terminal: high-only, and Omaha Hi/Lo
 * 8-or-better.  Hi/Lo is a different distribution of the same pot, not a
 * different game: each slice is split in half between the best high hand and
 * the best qualifying low hand, and the two halves are decided independently,
 * so a player can win one, both (scoop) or a share of each (quartering).
 */

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

/* Omaha Hi/Lo 8-or-better, same policy, ranges and reporting as above.
 *
 * For every side pot: no qualifying low means the whole slice goes to the best
 * high hand; otherwise half the slice goes to the best high hand and half to
 * the best qualifying low hand.  High and low ties split their own half
 * independently, so heads-up quartering is 75% to the high winner and 25% to
 * the player sharing the low.  The low is A-5 (ace low), straights and flushes
 * do not disqualify it, and it must use exactly two hole cards and three board
 * cards.  Values stay zero-sum: the awards are exactly the pot. */
int pe_omaha_hilo8_river_range_values(
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
