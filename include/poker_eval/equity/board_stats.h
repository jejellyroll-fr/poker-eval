/*
 * board_stats.h - Board scoring utilities for importance sampling/stratification
 */

#ifndef POKER_EVAL_EQUITY_BOARD_STATS_H
#define POKER_EVAL_EQUITY_BOARD_STATS_H

#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Heuristic score for a partial board (the newly drawn cards only),
 * based on pairedness/suitedness/straightish features.
 * Returns >= 0.5 (baseline), larger means “more decisive/critical” board.
 */
double pe_board_partial_score(StdDeck_CardMask partial_missing_cards,
                              int num_cards_drawn);

/* Pair-class stratification for K in {1,2,3} newly drawn cards.
 * Classes are mutually exclusive and exhaustive for K=3:
 *  - DISTINCT: 3 distinct ranks
 *  - ONE_PAIR: exactly one pair
 *  - TRIPS:    three of a kind
 * For K=2: DISTINCT and ONE_PAIR, TRIPS=0. For K=1: only DISTINCT.
 */
typedef enum {
    PE_BPAIR_DISTINCT = 0,
    PE_BPAIR_ONE_PAIR = 1,
    PE_BPAIR_TRIPS    = 2
} pe_bpair_class_t;

/* Classify the pair-structure of a (partial) board */
pe_bpair_class_t pe_board_pairclass(StdDeck_CardMask cards);

/* Compute analytic counts per class for selecting K cards uniformly from
 * the available deck (full deck minus dead). Returns class counts and total.
 */
typedef struct {
    uint64_t count[3];
    uint64_t total;   /* C(N,K) */
} pe_bpair_counts_t;

pe_bpair_counts_t pe_board_pair_counts(StdDeck_CardMask dead, int k);

/* Sample K cards uniformly conditioned on pair-class from the available deck
 * (full deck minus dead). Returns 1 on success, 0 if class has zero mass.
 */
int pe_board_sample_pairclass(StdDeck_CardMask dead, int k,
                              pe_bpair_class_t cls,
                              StdDeck_CardMask* out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_EQUITY_BOARD_STATS_H */
