/*
 * pe_holdem_river.h - exact Hold'em high terminal for river states
 *
 * This is the first concrete variant module for the generic vector lane. It
 * deliberately owns showdown evaluation only; betting transitions remain in
 * pe_betting_state and future street/chance transitions will be separate.
 */

#ifndef POKER_EVAL_PE_HOLDEM_RIVER_H
#define POKER_EVAL_PE_HOLDEM_RIVER_H

#include <stdint.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_holdem_deals.h>
#include <poker_eval/solver/pe_pots.h>
#include <poker_eval/solver/pe_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const EvalContext *context;
    mask_t board;
    /* Player-major layout: hole[player * combo_count + combo]. */
    const mask_t *hole;
    uint16_t combo_count;
} pe_holdem_river_spec_t;

/* Compare exact Hold'em high terminal values for a two-player river state. */
int pe_holdem_river_terminal_values(
    const pe_holdem_river_spec_t *spec,
    const pe_betting_state_t *state,
    const pe_reach_vec_t *reach,
    pe_value_vec_t *out_values,
    uint8_t player_count);

/*
 * Exact weighted river showdown for correlated two-card ranges.  The result
 * is net EV per player (payout minus that player's invested amount).  The
 * function applies card removal before normalising range weights and supports
 * up to PE_BETTING_MAX_PLAYERS players and side-pot slices.
 */
int pe_holdem_river_range_values(
    const EvalContext *context,
    mask_t board,
    const pe_holdem_range_t *ranges,
    const pe_betting_state_t *state,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_HOLDEM_RIVER_H */
