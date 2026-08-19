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

#endif /* POKER_EVAL_DRAW_OPTIMIZER_H */
