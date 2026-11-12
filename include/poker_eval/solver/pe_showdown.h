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
    PE_SHOWDOWN_PATH_PAIRWISE,
    PE_SHOWDOWN_PATH_MULTIWAY,
    /* Four-card ranges through the blocker-aware sweep: opponent reach is
       accumulated per card subset and removed by inclusion-exclusion. */
    PE_SHOWDOWN_PATH_PLO4
} pe_showdown_path_t;

typedef struct
{
    const mask_t *masks;
    const int64_t *strength;
    const double *reach;
    size_t combo_count;
} pe_showdown_player_t;

typedef struct
{
    double amount;
    uint8_t eligible_players;
} pe_showdown_sidepot_t;

/**
 * Compute the per-hero-combo showdown value.
 *
 * Strengths are ordered from worst to best. A win pays `pot`, a tie pays half
 * and a loss pays zero. Opponent reach is removed for board and hand blockers.
 * Two-card ranges use a sorted prefix path; four-card ranges of at least
 * PE_SHOWDOWN_PLO4_MIN_OPPONENTS entries use the inclusion-exclusion sweep
 * ("PLO4" path); anything else uses the exact pairwise fallback.
 *
 * The sweep sorts both ranges once and then walks opponent strengths in
 * ascending order, accumulating reach per nonempty card subset of each
 * opponent hand. For a hero hand the reach of every compatible opponent is
 * total minus the reach of the opponents sharing at least one card with it,
 * expanded over the hero's 15 nonempty subsets. Cost is O((opp_n + hero_n) *
 * log + 15 * (opp_n + hero_n)); memory is the subset table, bounded by the
 * number of distinct card subsets, not by opp_n * heroes.
 *
 * The inclusion-exclusion sum subtracts positive terms from a larger one, so
 * a compatible mass that is mathematically zero can come back slightly
 * negative; the sweep reports zero when the residue stays within
 * 1e-9 * (1 + accumulated reach) of it. Results otherwise agree with the
 * pairwise oracle to floating-point rounding.
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

/** Minimum opponent count before the four-card sweep is worth its setup. */
#define PE_SHOWDOWN_PLO4_MIN_OPPONENTS 32u

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

/**
 * Exact multiway showdown for two to eight players.
 *
 * The returned value for one combo excludes that player's own reach. The
 * conservation identity is therefore reach-weighted: summing
 * `reach[p][combo] * value[p][combo]` over all players gives the pot times the
 * compatible joint reach mass. Equal best hands split the pot equally.
 */
pe_solver_status_t pe_showdown_multiway_vector(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    mask_t dead,
    double pot,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path);

/** Multiway showdown with explicit pot amounts and eligible-player masks. */
pe_solver_status_t pe_showdown_multiway_sidepots(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    mask_t dead,
    const pe_showdown_sidepot_t *sidepots,
    size_t sidepot_count,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path);

/** Multiway fold value: all other players fold and the selected player wins. */
pe_solver_status_t pe_fold_multiway_vector(
    const pe_showdown_player_t *players,
    uint8_t player_count,
    uint8_t hero_player,
    mask_t dead,
    double pot,
    pe_value_vec_t *out_values,
    pe_showdown_path_t *out_path);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_SHOWDOWN_H */
