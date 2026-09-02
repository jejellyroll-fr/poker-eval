/*
 * cfr_locks.h - Node locking and periodic relocking (EXT-08)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * A locked infoset is one whose action frequencies are pinned to a target
 * instead of being learned. Two modes exist, and telling them apart is most of
 * what this module is for:
 *
 *   Freeze (#118)          the descent always plays the target, and neither
 *                          regret nor the average is updated there.
 *   Periodic relock (#11)  the node keeps learning between relock iterations,
 *                          so the un-locked actions retain true best-response
 *                          EVs; every lock_period iterations the average is
 *                          snapped back to the target and the exact EV cost of
 *                          the forced mix is measured.
 *
 * That second mode is why this is worth its own file. Measuring the cost means
 * running a full best-response walk per action from inside the traversal, and
 * leaving fifty lines of it in the middle of the recursion made the recursion
 * hard to read and the measurement hard to find.
 *
 * Internal to the engine, for the same reason as cfr_traversal.h: the walk it
 * performs calls into poker_engine.
 */

#ifndef POKER_EVAL_CFR_LOCKS_H
#define POKER_EVAL_CFR_LOCKS_H

#include "cfr_algo_ops.h"

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cfr_walk_ctx_t_fwd;

/* Everything the lock machinery needs to know about the node being visited.
   Bundled because the relock measurement genuinely needs all of it, and a
   thirteen-parameter function would hide that rather than justify it. */
typedef struct
{
    cfr_game_t *game;
    cfr_storage_t *storage;
    void *user_data;
    /* Carries the depth-exceeded flag and the telemetry adapter the
       best-response walks report through. */
    void *walk;
    uint64_t state_key;
    uint64_t infoset_key;
    const int *actions;
    int num_actions;
    int street;
    int acting_player;
} cfr_lock_node_t;

/* What the lock machinery decided for this node. */
typedef struct
{
    int is_locked;
    /* The pinned frequencies, or NULL when the node is not locked. Owned by
       the storage; valid until the next mutating call on it. */
    const double *target;
    int relock_mode;
    int relock_iter;
} cfr_lock_state_t;

/**
 * Settle the descent strategy for a node.
 *
 * Queries the lock and, when the node is locked, overwrites `strategy` with
 * either the target (freeze, or a relock iteration) or the regret-matched
 * strategy (a drifting iteration in relock mode). Leaves `strategy` untouched
 * for an unlocked node.
 */
void cfr_lock_begin_node(const cfr_lock_node_t *node,
                         const cfr_config_t *config,
                         int iter,
                         double *strategy,
                         cfr_lock_state_t *out_state);

/**
 * The update path of a locked node in relock mode.
 *
 * Regret keeps accumulating so the un-locked actions retain true
 * best-response EVs. On a relock iteration the average is snapped back to the
 * target and the exact EV loss of the forced mix is recorded: for each action
 * the child subtree's best-response value for the acting player is recomputed
 * with that player free everywhere below, so the loss isolates the cost of the
 * forced mix at this node alone.
 *
 * Only called when the node is locked and relock mode is on; a frozen node
 * updates nothing.
 */
void cfr_lock_apply_relock_update(const cfr_lock_node_t *node,
                                  const cfr_lock_state_t *state,
                                  const double *regret_delta,
                                  const double *reach);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CFR_LOCKS_H */
