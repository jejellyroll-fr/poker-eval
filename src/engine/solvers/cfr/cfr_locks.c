/*
 * cfr_locks.c - Node locking and periodic relocking (EXT-08)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Moved out of the traversal verbatim. The arithmetic is untouched: this
 * ticket's requirement is that the three lock tests report the same numbers as
 * before, so nothing here is an improvement on what it replaces — it is the
 * same code with a name and a boundary.
 */

#include "cfr_locks.h"
#include "cfr_traversal.h"

#include <stddef.h>

void cfr_lock_begin_node(const cfr_lock_node_t *node,
                         const cfr_config_t *config,
                         int iter,
                         double *strategy,
                         cfr_lock_state_t *out_state)
{
    cfr_storage_t *storage = node->storage;

    out_state->is_locked = 0;
    out_state->target = NULL;

    if (storage)
        out_state->is_locked = cfr_storage_get_locked_strategy(
            storage, node->infoset_key, node->num_actions, &out_state->target);

    out_state->relock_mode = config->enable_periodic_relock && config->lock_period > 0;
    out_state->relock_iter = out_state->relock_mode &&
                             (((iter + 1) % config->lock_period) == 0);

    if (!out_state->is_locked)
        return;

    if (out_state->relock_mode && !out_state->relock_iter)
    {
        /* Periodic relock (FEAT-11): let the node drift under normal
           regret-matching so the un-locked actions keep true (bounty-free)
           EVs, then re-assert the target only on relock iterations. */
        cfr_storage_get_regret_strategy_at_street(storage, node->infoset_key,
                                                  node->num_actions, node->street,
                                                  strategy);
    }
    else
    {
        /* Freeze mode (#118) or a relock iteration: force the descent
           strategy to the locked target frequencies. */
        for (int i = 0; i < node->num_actions; ++i)
            strategy[i] = out_state->target[i];
    }
}

void cfr_lock_apply_relock_update(const cfr_lock_node_t *node,
                                  const cfr_lock_state_t *state,
                                  const double *regret_delta,
                                  const double *reach)
{
    cfr_game_t *game = node->game;
    cfr_storage_t *storage = node->storage;
    cfr_walk_ctx_t *walk = (cfr_walk_ctx_t *)node->walk;

    /* Regret keeps accumulating normally so the un-locked actions retain true
       best-response EVs. */
    cfr_storage_update_regret_at_street(storage, node->infoset_key,
                                        node->num_actions, node->street,
                                        regret_delta, 1.0);

    if (!state->relock_iter)
        return;

    cfr_storage_overwrite_avg_at_street(storage, node->infoset_key,
                                        node->num_actions, node->street,
                                        state->target);

    /* Exact EV loss (FEAT-11): the acting player is locked only at this
       infoset, so below it plays freely. For each action i we recompute the
       child subtree's recursive best-response value for the acting player
       (opponents follow their average strategy, the acting player maximizes at
       every downstream decision). The loss then isolates the cost of the forced
       mix at THIS node:
         br_value     = max_i  BR(child_i)
         forced_value = sum_i locked[i] * BR(child_i)
       both using the same "free below" baseline. Child keys are derived fresh
       here (the descent already released them) and released again after the
       best-response walk to avoid use-after-free. */
    {
        double br_value = -1e300;
        double forced_value = 0.0;
        /* Use the multiway recursive best response whenever the game exposes a
           current_player callback (correct for N players and for 2-player games
           that provide one); otherwise fall back to the 2-player variant that
           derives the opponent as 1 - acting_player. */
        int use_multiway_br = game->current_player ? 1 : 0;
        double reach_weight;

        for (int i = 0; i < node->num_actions; ++i)
        {
            uint64_t br_child_key = game->apply_action(game, node->state_key,
                                                       node->actions[i],
                                                       node->user_data);
            double v;
            if (use_multiway_br)
            {
                v = cfr_best_response_recursive_multiway(game, storage,
                                                         node->acting_player,
                                                         br_child_key, node->user_data,
                                                         walk->recursion_depth + 1,
                                                         &walk->depth_exceeded,
                                                         walk->telemetry);
            }
            else
            {
                v = cfr_best_response_recursive(game, storage, node->acting_player,
                                                1 - node->acting_player, br_child_key,
                                                node->user_data,
                                                walk->recursion_depth + 1,
                                                &walk->depth_exceeded,
                                                walk->telemetry);
            }
            if (v > br_value)
                br_value = v;
            forced_value += state->target[i] * v;
            if (game->release_state)
                game->release_state(game, br_child_key, node->user_data);
        }

        /* Counterfactual reach of the acting player at this infoset, used to
           reach-weight the EV-loss aggregation across all its states. */
        reach_weight = reach[node->acting_player];
        if (reach_weight < 0.0)
            reach_weight = 0.0;
        cfr_storage_record_lock_ev_loss(storage, node->infoset_key, br_value,
                                        forced_value, reach_weight);
    }
}
