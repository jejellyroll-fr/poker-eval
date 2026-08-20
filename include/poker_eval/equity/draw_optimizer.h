/*
 * Generic Draw Decision & Equity Optimizer
 *
 * Computes the exact expected equity of every one of the 32 possible
 * discard masks for a 5-card draw game, and returns the optimal discard.
 *
 * For a 5-card hand there are 2^5 = 32 possible discard bitmasks D in {0..31}.
 * Given D, let k = |D| be the number of cards discarded, H_D the kept subset
 * and R a replacement set of k cards drawn uniformly from the remaining unseen
 * cards. The expected equity of discard choice D is:
 *
 *   EV(D) = (1 / C(N_dead, k)) * sum_R V(H_D u R, B)
 *
 * where V(H, B) is a per-game value function mapping a resulting hand to a
 * number in [0, 1] (the hand's normalized strength against a random hand of
 * the same kind) and N_dead = 52 - |hand| - |board| - |dead|.
 *
 * The optimal draw decision is D* = argmax_D EV(D).
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 */

#ifndef POKER_EVAL_DRAW_OPTIMIZER_H
#define POKER_EVAL_DRAW_OPTIMIZER_H

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/pokereval_export.h>

/* A single discard option: its bitmask, how many cards it draws, and the
 * computed expected equity (0.0 .. 1.0). */
typedef struct {
    int discard_mask;       /* Bitmask (0..31) indicating kept/discarded cards */
    int cards_drawn;        /* Number of cards replaced (0..5) */
    double expected_equity; /* Calculated EV (0.0 .. 1.0) */
} pe_draw_option_t;

/* Full result of a draw optimization: all 32 options plus the optimum. */
typedef struct {
    pe_draw_option_t options[32];
    int num_options;
    int optimal_mask;
    double max_equity;
} pe_draw_result_t;

/* Computes EV for all 32 discard masks and finds the optimal discard for any
 * of the supported draw games. Returns 0 on success, non-zero on error
 * (invalid game, malformed hand, or NULL out_result). On success, every entry
 * of out_result->options is populated (index == discard mask) and
 * out_result->optimal_mask holds the argmax. */
POKEREVAL_EXPORT int pe_compute_draw_optima(
    enum_game_t game,
    StdDeck_CardMask hand,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    pe_draw_result_t *out_result);

/* Returns the normalized strength in [0, 1] of a single hand for the given
 * game (the V(H, B) value used inside pe_compute_draw_optima). This is the
 * exact equity the optimizer would assign to the "keep everything" mask and
 * is exposed so callers and tests can reason about individual hands. */
POKEREVAL_EXPORT int pe_draw_hand_strength(
    enum_game_t game,
    StdDeck_CardMask hand,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    double *out_strength);

/* Value function callback for pe_compute_draw_optima_fn: maps a resulting
 * 5-card hand to a comparable number, with higher values preferred by the
 * optimizer. Unlike the built-in equity value (a normalized strength in
 * [0, 1] against a random hand of the same kind), the callback is free to
 * return any comparable quantity -- e.g. a video poker paytable payout, an
 * unbounded multiplier. `ctx` is an opaque caller context.
 *
 * The callback is invoked from within pe_compute_draw_optima_fn only, and
 * must be reentrant: the optimizer is thread-safe and callers may share one
 * callback/context across OpenMP threads. */
typedef double (*pe_draw_value_fn)(StdDeck_CardMask hand, void *ctx);

/* Computes EV for all 32 discard masks using a caller-supplied value
 * function instead of the built-in per-game equity. This generalises the
 * objective of pe_compute_draw_optima: a paytable-aware caller can pass a
 * function returning the expected *payout* of the resulting hand, which the
 * equity objective cannot express (e.g. breaking a made flush to draw to a
 * royal is correct under a paytable that pays 800-to-1, and never correct
 * under an equity objective).
 *
 * The enumeration is otherwise identical to pe_compute_draw_optima: the
 * unseen pool is the 52-card deck minus hand, board and dead cards, and the
 * expected value of discard mask D is the mean of value_fn over every
 * replacement set. pe_draw_result_t.expected_equity therefore carries the
 * callback's value space (which need not be [0, 1]).
 *
 * Returns 0 on success, non-zero on error (NULL out_result or value_fn, or a
 * hand that is not exactly 5 cards). pe_compute_draw_optima's behaviour is
 * unchanged; it remains the entry point for the built-in equity objective. */
POKEREVAL_EXPORT int pe_compute_draw_optima_fn(
    StdDeck_CardMask hand,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    pe_draw_value_fn value_fn,
    void *ctx,
    pe_draw_result_t *out_result);

#endif /* POKER_EVAL_DRAW_OPTIMIZER_H */
