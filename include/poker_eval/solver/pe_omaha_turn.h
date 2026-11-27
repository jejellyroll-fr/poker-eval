/* pe_omaha_turn.h - exact PLO4/PLO5/PLO6 turn-to-river terminal
 *
 * Issue #285, phase A.  The river terminal (pe_omaha_river.h) prices one
 * five-card board.  This one prices a four-card board by enumerating the
 * chance space explicitly: every river card still in the deck is dealt once,
 * the river terminal is evaluated on the five-card board it makes, and the
 * results are combined by their deal mass.  Nothing is sampled.
 *
 * Deal distribution
 * -----------------
 * A deal is a tuple (hero hands, river card).  The hands carry the range
 * weights, so the weight of a deal is w_0 w_1 for a legal pair, and the river
 * is uniform over the cards neither the board nor any hand holds: with two
 * four-card hands on a four-card board that is 52 - 8 - 4 = 40 cards, fewer
 * when `dead` is declared.  Because the pooled value is
 *
 *     sum over rivers of (deal mass on that river) * (its conditional value)
 *     -------------------------------------------------------------------
 *                sum over rivers of (deal mass on that river)
 *
 * and every legal pair keeps exactly the same number of candidate rivers, the
 * ratio is the chance-expanded mean over deals.  The per-river conditional
 * value is the river terminal's own convention: net of the player's
 * investment, divided by the reach mass of that river's legal deals.
 *
 * Board-dependent legality
 * ------------------------
 * A combo that holds the river card cannot be a private hand on that river,
 * so it is removed from the range before the river terminal runs; the terminal
 * then removes the pairs its board and hand blockers remove.  This is the turn
 * counterpart of the river's dead-card handling and is what the phase A
 * oracle tests pin.
 *
 * Reporting
 * ---------
 * `*out_river_count` counts the river cards the terminal actually priced: the
 * cards the board and the dead mask leave, so 48 for a full PLO4 turn and
 * 48 - popcount(dead) when cards are dead, minus any river on which no pair is
 * compatible.  A river a private hand holds is removed per pair inside the
 * enumeration, which is why the count does not shrink with the ranges.
 * `*out_weight_sum` is the total chance-expanded deal mass (the denominator
 * above): the rivers each legal pair can see, times the pooled range mass.  A
 * caller that wants the plain hand distribution must divide by it, exactly as
 * with the river terminal.
 *
 * What this is not
 * ----------------
 * There is no turn betting tree here and no turn certificate: the solver's
 * exploitability guarantee stays the PLO4 river one of issue #283, because a
 * claim over two streets would have to enumerate the river decisions after
 * each chance outcome rather than only the showdown.  This terminal is the
 * exact showdown layer such a tree would need, and the fixture it is tested
 * against.
 *
 * Hi/Lo has its own entry point, pe_omaha_hilo8_turn_range_values(): every
 * river is priced by the river terminal's Hi/Lo payoff, and the chance
 * expansion, the per-river filter and the reduction are the high-only ones.
 */

#ifndef POKER_EVAL_PE_OMAHA_TURN_H
#define POKER_EVAL_PE_OMAHA_TURN_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_omaha_deals.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum players the terminal accepts, matching the river terminal. */
#define PE_OMAHA_TURN_PLAYERS_MAX 8u

/* Cards the board must already hold: the turn. */
#define PE_OMAHA_TURN_BOARD_CARDS 4

/*
 * Where a turn call spends its time, and what it touched to get there.
 *
 * The terminal has two phases per candidate river: a filter that copies the
 * combos not holding the river card into a compact range, and one river
 * terminal call on the five-card board that filter makes.  `filter_seconds`
 * and `terminal_seconds` are monotonic-clock totals taken inside the call, and
 * the counters are exact, so the two shares can be compared with the caller's
 * own wall clock for the same call.  The remainder is validation, allocation
 * and the final reduction, which is why a small range still shows a visible
 * unaccounted share.
 *
 * `deals` sums the river terminal's own deal count.  That count is 0 on the
 * heads-up four-card vector sweep, which evaluates combos rather than tuples;
 * `deals` is then 0 and no cost per deal can be formed for that configuration.
 *
 * The struct is zeroed on entry on every path, failures included, so a caller
 * that ignores the status reads zeros rather than a previous call's figures.
 */
typedef struct
{
    size_t combos_examined;    /* combos the per-river filter looked at */
    size_t combos_kept;        /* combos that survived the filter */
    size_t rivers_to_terminal; /* rivers that reached the river terminal */
    size_t deals;              /* tuples the river terminal counted */
    double filter_seconds;     /* time in the per-river filter */
    double terminal_seconds;   /* time in the river terminal */
} pe_omaha_turn_stats_t;

/*
 * Exact turn-to-river values for `player_count` ranges.
 *
 * `board` is the four-card turn board, `dead` the cards known to be out of
 * play (disjoint from the board and from every declared combo; MASK_EMPTY when
 * there are none).  `ranges` and `state` follow the river terminal: ranges are
 * weighted combo lists of `hole_cards` cards, `state` carries the pot, the
 * per-player investment and the active players of the terminal being priced,
 * with `winner < 0` (a fold is not a showdown).
 *
 * Returns 0 with `out_values` in chips per deal, -1 on invalid input (which
 * includes a NULL output pointer), -2 when an allocation fails, and 1 when the
 * enumeration found no legal deal.  The outputs are zeroed on every path.
 */
int pe_omaha_turn_range_values(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum);

/*
 * The same terminal, also filling `stats`.  The values, the river count and the
 * deal mass are bit-identical to the call above: this entry point adds counters
 * and two clocks, not a second arithmetic.  `stats` is required here; the plain
 * entry point is the one with no instrumentation at all.
 */
int pe_omaha_turn_range_values_profiled(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum,
    pe_omaha_turn_stats_t *stats);

/*
 * Cost split of the same turn, for the frozen-strength probe of issue #332:
 * what the terminal pays once per combo per river, and what its deal walks pay
 * per visit with the strengths already there.  The river terminal hoists that
 * precompute by design A of the issue, so these are the two parts of the walk
 * it always runs, summed over the rivers a turn prices.  On a Hi/Lo turn the
 * probe fills the low table in a pass of its own and reports it in
 * `low_precompute_seconds`, summed over the rivers; that field stays zero on
 * the high-only path.
 *
 * The values are those of pe_omaha_turn_range_values(), which the caller has to
 * check: a probe that does not reproduce the fingerprint is measuring a
 * different walk and its number is void.
 *
 * The two instrumentations are not read together.  A caller that asks for both
 * gets `terminal_seconds` around the frozen river call, its precompute
 * included, rather than around the live terminal, so that figure is not the
 * profile's terminal share and the two blocks cannot be subtracted from each
 * other.  Price the walk against the precompute here, and take the filter and
 * the live terminal from pe_omaha_turn_range_values_profiled().
 */
typedef struct
{
    double precompute_seconds;
    double low_precompute_seconds;
    double walk_seconds;
    size_t deals;
    size_t combos;
} pe_omaha_turn_probe_t;

/*
 * The same terminal on Hi/Lo boards.  `stats` and `probe` are optional and mean
 * what they mean above: the same counters, and the same split of the hoisted
 * walk, with the low table's precompute reported apart.  The values, the river
 * count and the deal mass of this entry are those of a Hi/Lo river terminal run
 * on every candidate river, combined by the high-only chance expansion.
 */
int pe_omaha_hilo8_turn_range_values(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum,
    pe_omaha_turn_stats_t *stats,
    pe_omaha_turn_probe_t *probe);

int pe_omaha_turn_range_values_frozen(
    const EvalContext *context,
    mask_t board,
    mask_t dead,
    const pe_omaha_range_t *ranges,
    const pe_betting_state_t *state,
    uint8_t hole_cards,
    double *out_values,
    uint8_t player_count,
    size_t *out_river_count,
    double *out_weight_sum,
    pe_omaha_turn_probe_t *probe);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_OMAHA_TURN_H */
