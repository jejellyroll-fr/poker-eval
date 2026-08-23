/*
 * pe_blockers.h - Card removal at terminal nodes (architecture v3, RNG-05)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Two players cannot hold the same card. At a terminal node that means the
 * opponent reach a hero combo actually faces is not the whole range but the
 * part of it that does not use the hero's cards — and the difference is not
 * small: with both sides holding "AA", six of thirty-six pairs survive.
 *
 * Doing that by pairing every hero combo with every opponent combo is
 * O(n*m) — 270725^2 for two PLO ranges, which is the wall RNG-03's enumeration
 * cap runs into. The way out is inclusion-exclusion over 52 per-card
 * accumulators: the opponent reach a hero hand blocks is the sum over its
 * cards of what each card carries, minus what was counted twice. That is O(n)
 * for the setup and O(1) per hero combo.
 *
 * The fast path covers two-card hands, where the only double count is the
 * opponent combo made of exactly the hero's two cards. Wider hands take an
 * exact pairwise fallback rather than an approximation — a blocker correction
 * that is nearly right produces a solve that is quietly wrong, and PLO gets
 * its own inclusion-exclusion when the vector lane needs it.
 */

#ifndef POKER_EVAL_PE_BLOCKERS_H
#define POKER_EVAL_PE_BLOCKERS_H

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_vector.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which path pe_blockers_compatible_sum took, for tests and diagnostics. */
typedef enum {
    PE_BLOCKERS_PATH_NONE = 0,
    /** Inclusion-exclusion over per-card accumulators: O(n + m). */
    PE_BLOCKERS_PATH_ACCUMULATED,
    /** Exact pairwise fallback: O(n * m). Correct, and slow on purpose. */
    PE_BLOCKERS_PATH_PAIRWISE
} pe_blockers_path_t;

/**
 * Opponent reach compatible with each hero combo.
 *
 * out[i] receives the sum of opp_reach[j] over every opponent combo j whose
 * cards intersect neither hero combo i nor `dead`. A hero combo that itself
 * touches `dead` gets 0: it cannot be held, so it faces nothing.
 *
 * `dead` is the board and any other known card. Removing it once here is what
 * keeps the caller from having to filter both ranges beforehand.
 *
 * @param hero_masks  One card mask per hero combo.
 * @param opp_masks   One card mask per opponent combo.
 * @param opp_reach   Reach probability per opponent combo, same order.
 * @param out         Receives hero_n values.
 * @param out_path    Which path was taken. May be NULL.
 *
 * @return PE_SOLVER_OK, PE_SOLVER_ERR_NULL_ARGUMENT, or
 *         PE_SOLVER_ERR_INVALID_CONFIG when either side is empty.
 */
pe_solver_status_t pe_blockers_compatible_sum(const mask_t *hero_masks,
                                              size_t hero_n,
                                              const mask_t *opp_masks,
                                              const double *opp_reach,
                                              size_t opp_n,
                                              mask_t dead,
                                              double *out,
                                              pe_blockers_path_t *out_path);

/**
 * The same, computed by pairing every combo with every combo.
 *
 * Exposed because it is the reference the fast path is checked against: the
 * two must agree, and a test that only exercised the fast path would be
 * testing it against itself.
 */
pe_solver_status_t pe_blockers_compatible_sum_pairwise(const mask_t *hero_masks,
                                                       size_t hero_n,
                                                       const mask_t *opp_masks,
                                                       const double *opp_reach,
                                                       size_t opp_n,
                                                       mask_t dead,
                                                       double *out);

/**
 * Fold payoff for the remaining player, per hero combo.
 *
 * This is the compatible opponent reach multiplied by `pot`, using the same
 * accumulated O(n + m) path for two-card hands and exact pairwise fallback for
 * wider hands. `out_values->n` must equal hero_n and its storage must be
 * writable.
 */
pe_solver_status_t pe_blockers_fold_vector(const mask_t *hero_masks,
                                           size_t hero_n,
                                           const mask_t *opp_masks,
                                           const double *opp_reach,
                                           size_t opp_n,
                                           mask_t dead,
                                           double pot,
                                           pe_value_vec_t *out_values,
                                           pe_blockers_path_t *out_path);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_BLOCKERS_H */
