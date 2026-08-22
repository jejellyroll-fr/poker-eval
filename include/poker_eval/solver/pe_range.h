/*
 * pe_range.h - Private ranges for the solver (architecture v3, RNG-01)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Almost nothing here is new, and that is the point. The repository already
 * carries pe_combo_t and pe_range_t in <poker_eval/range.h> — a weighted card
 * mask and an array of them — which is exactly what architecture v3 §7.1
 * describes as pe_hand_combo_t and pe_player_range_t. Defining those again
 * would have produced two range types for one concept.
 *
 * What the solver needs on top is not a type but a guarantee. A traversal
 * indexes combos by position, thousands of times per node; it cannot afford to
 * re-check that the array has no duplicates, that the order is the same as
 * last iteration, or that the weights mean what it assumes. So the solver
 * takes a parsed range once and establishes the invariants:
 *
 *   non-empty        an empty range is a configuration error, not a range
 *                    that happens to reach nothing
 *   deduplicated     one entry per distinct hand, weights of duplicates summed
 *   stably ordered   sorted by card mask, so combo index i means the same hand
 *                    on every iteration and in every checkpoint
 *   normalised       weights sum to 1, so a reach probability is a probability
 *
 * Parsing is deliberately not here. Turning "AKs" into card masks is an
 * adapter's job — see pe_solver_range_parse() — and the domain only ever sees
 * combos.
 */

#ifndef POKER_EVAL_PE_RANGE_H
#define POKER_EVAL_PE_RANGE_H

#include <poker_eval/range.h>
#include <poker_eval/solver/pe_solver.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A borrowed, prepared range.
 *
 * Points into a pe_range_t the caller owns; the solver never frees it. Holding
 * a view rather than a copy is what lets several players share one range
 * without the solver deciding who owns it.
 *
 * Only valid while the underlying pe_range_t is alive and unmodified.
 */
typedef struct {
    const pe_combo_t *combos;
    size_t count;
} pe_range_view_t;

/**
 * Establish the solver's invariants on a parsed range, in place.
 *
 * Sums the weights of duplicate hands, sorts by card mask, drops entries whose
 * weight is not positive, and normalises the rest to sum to 1. Idempotent:
 * preparing an already-prepared range changes nothing.
 *
 * A weight that is negative or not finite is an error rather than something to
 * clamp — it means the caller computed it wrong, and silently repairing it
 * would hide that in a solve whose numbers then look plausible.
 *
 * @return PE_SOLVER_OK,
 *         PE_SOLVER_ERR_NULL_ARGUMENT,
 *         PE_SOLVER_ERR_INVALID_CONFIG when the range is empty, holds no
 *         positive weight, or holds a negative or non-finite one.
 */
pe_solver_status_t pe_solver_range_prepare(pe_range_t *range);

/**
 * View over a prepared range.
 *
 * Does not check the invariants — pe_solver_range_prepare() established them,
 * and re-verifying on every access is what the preparation exists to avoid.
 */
pe_range_view_t pe_solver_range_view(const pe_range_t *range);

/**
 * Whether a range satisfies the invariants.
 *
 * For tests and for an assertion at the start of a solve; not for the hot
 * path. `tolerance` bounds how far the weights may sum from 1.
 */
int pe_solver_range_is_prepared(const pe_range_t *range, double tolerance);

/* ------------------------------------------------------------------ *
 * Adapter: the range parser
 * ------------------------------------------------------------------ */

/**
 * Parse a range string and prepare it in one step.
 *
 * A convenience over <poker_eval/range.h>'s pe_range_parse() followed by
 * pe_solver_range_prepare(). Declared here rather than in the domain because
 * parsing is an adapter concern: the domain sees combos, never syntax.
 *
 * The caller owns the result and releases it with pe_range_free().
 *
 * @return PE_SOLVER_OK, or the failure that stopped it. *out_range is NULL on
 *         failure, so a caller cannot mistake a rejected string for an empty
 *         range.
 */
pe_solver_status_t pe_solver_range_parse(enum_game_t variant,
                                         const char *range_str,
                                         StdDeck_CardMask dead_cards,
                                         pe_range_t **out_range);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_RANGE_H */
