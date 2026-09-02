/*
 * pe_combinations.h - Indexing k-subsets (architecture v3, CHN-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * A flop is three cards dealt at once, not three cards dealt in a row. The
 * difference is not cosmetic: a chance node that dealt them sequentially would
 * offer 48*47*46 outcomes where there are C(48,3), and would weight every flop
 * six times — once per ordering — which is six times the probability the deck
 * actually gives it.
 *
 * So the outcomes have to be combinations, and a chance node addresses its
 * outcomes by index. That needs a bijection between [0, C(n,k)) and the
 * k-subsets of [0, n), which is what this provides.
 *
 * The order is colexicographic, chosen because its unranking is a plain
 * descending scan with no division and no table: the rank of {c0 < c1 < ...}
 * is the sum of C(ci, i+1). It also has the property that the ranks of the
 * k-subsets of [0, m) are exactly [0, C(m,k)) for every m <= n, so an index
 * stays valid when the deck grows — which matters because the number of unused
 * cards changes from node to node.
 */

#ifndef POKER_EVAL_PE_COMBINATIONS_H
#define POKER_EVAL_PE_COMBINATIONS_H

#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest k this module indexes. Three for a flop, five for a full board. */
#define PE_COMB_MAX_K 8

/**
 * C(n, k), saturating at UINT64_MAX rather than overflowing.
 *
 * @return The count; 0 when k > n, and 1 when k is 0.
 */
uint64_t pe_comb_count(unsigned n, unsigned k);

/**
 * The `rank`-th k-subset of [0, n), in colexicographic order.
 *
 * Writes k strictly increasing values into `out`. The mapping is a bijection:
 * every rank in [0, C(n,k)) yields a distinct subset, and every subset has
 * exactly one rank — which is what keeps a flop from being dealt twice under
 * two different orderings.
 *
 * @return PE_SOLVER_OK, PE_SOLVER_ERR_NULL_ARGUMENT, or
 *         PE_SOLVER_ERR_INVALID_CONFIG when k is 0 or above PE_COMB_MAX_K, or
 *         `rank` is out of range.
 */
pe_solver_status_t pe_comb_unrank(unsigned n, unsigned k, uint64_t rank,
                                  unsigned *out);

/**
 * The rank of a k-subset, the inverse of pe_comb_unrank.
 *
 * `values` must be strictly increasing. Exposed so a test can check the round
 * trip rather than trusting one direction.
 *
 * @return PE_SOLVER_OK, or PE_SOLVER_ERR_INVALID_CONFIG when the input is not
 *         a strictly increasing k-subset of [0, n).
 */
pe_solver_status_t pe_comb_rank(unsigned n, unsigned k, const unsigned *values,
                                uint64_t *out_rank);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_COMBINATIONS_H */
