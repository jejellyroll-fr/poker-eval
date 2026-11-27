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
#include <poker_eval/solver/pe_showdown.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Both entry points report `*out_deal_count` exactly on the enumeration path.
 * The heads-up four-card high-only vector path does not count deals: it reports
 * 0 there, because the field is telemetry and counting the compatible pairs is
 * work the payoff does not need.  `*out_weight_sum` is exact on both paths. */
/*
 * Cost split of one river terminal call, for the frozen-strength probe of
 * issue #332.  The terminal evaluates every combo once against the board before
 * it visits a single deal (design A of that issue), so a call is two parts:
 * `precompute_seconds` is that one `StdDeck_OmahaHi_EVAL` per combo of every
 * range, and `walk_seconds` is the deal walk reading those strengths from an
 * array, with the same payoff and the same accumulation order.  High-only:
 * Hi/Lo carries a second strength per combo and the probe does not cover it,
 * although the terminal itself hoists both halves.
 *
 * The probe is valid only if it reproduces the terminal bit for bit; the caller
 * checks the fingerprint, and a mismatch means the probe is measuring a
 * different walk and its number is void.
 */
typedef struct
{
    double precompute_seconds;
    double walk_seconds;
    size_t deals;
    size_t combos;
} pe_omaha_river_probe_t;

/*
 * The same terminal, timed in the two parts the hoisted walk has: the precompute
 * and the deal visits.  The values, the deal count and the deal mass are those
 * of pe_omaha_river_range_values(), which runs the same two parts without the
 * clocks; a caller still checks them, because a probe that does not reproduce
 * the terminal is measuring a different walk.
 *
 * `probe` is required and is zeroed on entry.  Hi/Lo and the heads-up four-card
 * vector sweep are not priced here: the probe always enumerates, so it times
 * the walk this terminal runs rather than the vector kernel.
 */
int pe_omaha_river_range_values_frozen(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum,
    pe_omaha_river_probe_t *probe);

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

/* Same terminal, and reports which evaluation path ran: PE_SHOWDOWN_PATH_PLO4
 * for the heads-up four-card vector path (precomputed strengths through
 * pe_showdown_vector()), PE_SHOWDOWN_PATH_NONE for deal enumeration.
 * `out_path` may be NULL. */
int pe_omaha_river_range_values_with_path(
    const EvalContext *context,
    mask_t board,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_deal_count,
    double *out_weight_sum,
    pe_showdown_path_t *out_path);

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
