/*
 * pe_showdown.h - Sorted vector showdown (architecture v3, VEC-06)
 */

#ifndef POKER_EVAL_PE_SHOWDOWN_H
#define POKER_EVAL_PE_SHOWDOWN_H

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_vector.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PE_SHOWDOWN_PATH_NONE = 0,
    PE_SHOWDOWN_PATH_SORTED,
    PE_SHOWDOWN_PATH_PAIRWISE
} pe_showdown_path_t;

/**
 * Compute the per-hero-combo showdown value.
 *
 * Strengths are ordered from worst to best. A win pays `pot`, a tie pays half
 * and a loss pays zero. Opponent reach is removed for board and hand blockers.
 * Two-card ranges use a sorted prefix path; wider or duplicated opponent
 * combos use the exact pairwise fallback.
 */
pe_solver_status_t pe_showdown_vector(const mask_t *hero_masks,
                                      const int64_t *hero_strength,
                                      size_t hero_n,
                                      const mask_t *opp_masks,
                                      const int64_t *opp_strength,
                                      const double *opp_reach,
                                      size_t opp_n,
                                      mask_t dead,
                                      double pot,
                                      pe_value_vec_t *out_values,
                                      pe_showdown_path_t *out_path);

/** Independent O(hero_n * opp_n) reference implementation. */
pe_solver_status_t pe_showdown_vector_pairwise(const mask_t *hero_masks,
                                               const int64_t *hero_strength,
                                               size_t hero_n,
                                               const mask_t *opp_masks,
                                               const int64_t *opp_strength,
                                               const double *opp_reach,
                                               size_t opp_n,
                                               mask_t dead,
                                               double pot,
                                               pe_value_vec_t *out_values);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SHOWDOWN_H */
