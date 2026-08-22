/*
 * cfr_traversal_full_scalar.c - Exhaustive scalar tree walk (EXT-04)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * One value per state, every action, every chance outcome. Slow, exact, and
 * the yardstick every other traversal is measured against — which is why it
 * moved out of cfr_core.c first: it has to stay readable and it must not
 * acquire branches for algorithms it does not implement.
 *
 * What this file does NOT contain is the point of the extraction. It never
 * mentions DCFR, linear averaging or ECFR; it asks cfr_algo_ops_t for a
 * discount and an averaging weight and applies them. Adding CFR+ later means
 * writing an ops implementation, not another `if` in the recursion.
 *
 * Locks and the periodic relock are still here. They belong to EXT-08, and
 * pulling them out in the same step as the move would have made a
 * bit-identical result impossible to argue about.
 */

#include "cfr_traversal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void cfr_traverse_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    const cfr_config_t *config,
    const cfr_algo_ops_t *algo,
    uint64_t state_key,
    double *reach,
    int num_players,
    int iter,
    double *out_util,
    void *user_data,
    double *scratch,
    int depth_limit,
    cfr_walk_ctx_t *walk)
{
    int actions[CFR_MAX_ACTIONS];
    int num_actions;
    int acting_player;
    double *strategy;
    double *regret_delta;
    double *action_util;
    double node_util_acting = 0.0;
    double node_util_vec[CFR_MAX_PLAYERS];
    double child_util[CFR_MAX_PLAYERS];
    double next_reach[CFR_MAX_PLAYERS];
    double reach_others = 1.0;
    double flow_weight = 1.0;
    double discount = 1.0;
    double t;
    double avg_weight;
    const double *locked = NULL;
    int is_locked = 0;
    uint64_t infoset_key;
    int street;

    for (int p = 0; p < num_players; ++p)
        out_util[p] = 0.0;

    if (config->stop_flag && *config->stop_flag)
        return;

    walk->recursion_depth++;
    if (walk->recursion_depth > depth_limit)
    {
        if (!walk->depth_exceeded)
        {
            pe_telemetry_emitf(walk->telemetry, PE_LOG_ERROR, "cfr", (uint64_t)walk->current_iter,
                               "[cfr] error: max recursion depth %d exceeded at 0x%llx; a cycle or runaway tree is likely\n",
                               depth_limit, (unsigned long long)state_key);
        }
        walk->depth_exceeded = 1;
        goto cfr_exit;
    }
    if (walk->max_depth < walk->recursion_depth)
    {
        walk->max_depth = walk->recursion_depth;
        if (config->trace_iterations)
        {
            pe_telemetry_emitf(walk->telemetry, PE_LOG_TRACE, "cfr", (uint64_t)walk->current_iter,
                               "[cfr] iter %d depth -> %d (state=0x%llx)\n",
                               walk->current_iter, walk->max_depth, (unsigned long long)state_key);
        }
    }
    walk->node_count++;
    infoset_key = cfr_traversal_storage_key(game, state_key);
    street = cfr_traversal_storage_street(game, state_key);

    if (game->is_terminal(game, state_key, user_data))
    {
        cfr_traversal_terminal_utilities(game, state_key, num_players, out_util, user_data);
        if (storage)
            cfr_storage_accumulate_ev_at_street(storage, infoset_key, street, out_util[0]);
        goto cfr_exit;
    }

    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0 || !game->apply_chance)
            goto cfr_exit;
        /* FEAT-14 (#150): weight chance outcomes (card bunching) and
           normalize by the total weight instead of assuming equally-likely
           outcomes. */
        double chance_weight_sum = 0.0;
        for (int p = 0; p < num_players; ++p)
            out_util[p] = 0.0;
        for (int c = 0; c < outcomes; ++c)
        {
            double w = cfr_chance_weight(game, state_key, c, user_data);
            chance_weight_sum += w;
            uint64_t child_key = game->apply_chance(game, state_key, c, user_data);
            cfr_traverse_recursive(game, storage, config, algo, child_key,
                                   reach, num_players, iter, child_util,
                                   user_data, scratch, depth_limit, walk);
            for (int p = 0; p < num_players; ++p)
                out_util[p] += w * child_util[p];
            if (game->release_state)
                game->release_state(game, child_key, user_data);
        }
        double chance_norm = (chance_weight_sum > 0.0)
                                 ? chance_weight_sum
                                 : (double)outcomes;
        for (int p = 0; p < num_players; ++p)
            out_util[p] /= chance_norm;
        goto cfr_exit;
    }

    num_actions = game->get_actions(game, state_key, actions, CFR_MAX_ACTIONS, user_data);
    if (num_actions <= 0)
    {
        for (int p = 0; p < num_players; ++p)
            out_util[p] = 0.0;
        goto cfr_exit;
    }
    if (num_actions > CFR_MAX_ACTIONS)
        num_actions = CFR_MAX_ACTIONS; /* keep scratch indexing in bounds */

    if (game->current_player)
    {
        acting_player = game->current_player(game, state_key, user_data);
        if (acting_player < 0 || acting_player >= num_players)
            acting_player = 0;
    } else {
        acting_player = 0;
    }

    if (config->trace_iterations)
    {
        pe_telemetry_emitf(walk->telemetry, PE_LOG_TRACE, "cfr", (uint64_t)walk->current_iter,
                           "[cfr] iter %d depth %d state 0x%llx actions=%d player=%d\n",
                           walk->current_iter, walk->recursion_depth, (unsigned long long)state_key, num_actions, acting_player);
    }

    /* Per-frame scratch, indexed by depth: each frame gets
       [strategy | regret_delta | action_util], each up to
       CFR_MAX_ACTIONS doubles. */
    size_t frame_off = (size_t)walk->recursion_depth * 3u * (size_t)CFR_MAX_ACTIONS;
    strategy = scratch + frame_off;
    regret_delta = scratch + frame_off + (size_t)CFR_MAX_ACTIONS;
    action_util = scratch + frame_off + 2u * (size_t)CFR_MAX_ACTIONS;

    cfr_storage_get_strategy_at_street(storage, infoset_key, num_actions, street, strategy);

    /* Frozen node (locked strategy). */
    if (storage)
        is_locked = cfr_storage_get_locked_strategy(storage, infoset_key, num_actions, &locked);

    int relock_mode = config->enable_periodic_relock && config->lock_period > 0;
    int relock_iter = relock_mode && (((iter + 1) % config->lock_period) == 0);

    if (is_locked)
    {
        if (relock_mode && !relock_iter)
        {
            /* Periodic relock (FEAT-11): let the node drift under normal
               regret-matching so the un-locked actions keep true (bounty-free)
               EVs, then re-assert the target only on relock iterations. */
            cfr_storage_get_regret_strategy_at_street(storage, infoset_key, num_actions, street, strategy);
        }
        else
        {
            /* Freeze mode (#118) or a relock iteration: force the descent
               strategy to the locked target frequencies. */
            for (int i = 0; i < num_actions; ++i)
                strategy[i] = locked[i];
        }
    }
    for (int p = 0; p < num_players; ++p)
        node_util_vec[p] = 0.0;


    for (int i = 0; i < num_actions; ++i)
    {
        uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
        if (config->trace_iterations)
        {
            pe_telemetry_emitf(walk->telemetry, PE_LOG_TRACE, "cfr", (uint64_t)walk->current_iter,
                               "[cfr] iter %d depth %d action %d -> state 0x%llx\n",
                               walk->current_iter, walk->recursion_depth, actions[i], (unsigned long long)next_state_key);
        }

        for (int p = 0; p < num_players; ++p)
            next_reach[p] = reach[p];
        next_reach[acting_player] *= strategy[i];

        cfr_traverse_recursive(
            game, storage, config, algo, next_state_key,
            next_reach, num_players, iter, child_util, user_data,
            scratch, depth_limit, walk);

        if (game->release_state)
            game->release_state(game, next_state_key, user_data);

        if (config->stop_flag && *config->stop_flag)
            goto cfr_exit;

        action_util[i] = child_util[acting_player];
        node_util_acting += strategy[i] * action_util[i];
        for (int p = 0; p < num_players; ++p)
            node_util_vec[p] += strategy[i] * child_util[p];
    }

    if (config->stop_flag && *config->stop_flag)
        goto cfr_exit;

    for (int p = 0; p < num_players; ++p)
    {
        if (p == acting_player)
            continue;
        reach_others *= reach[p];
    }

    if (walk->use_flow_focus)
    {
        double flow = 1.0;
        for (int p = 0; p < num_players; ++p)
            flow *= reach[p];
        if (flow < 1e-12)
            flow = 1e-12;
        if (fabs(walk->flow_pow - 1.0) > 1e-9)
            flow = pow(flow, walk->flow_pow);
        flow_weight = flow;
    }

    for (int i = 0; i < num_actions; ++i)
        regret_delta[i] = (action_util[i] - node_util_acting) * reach_others * flow_weight;

    /* Which formula produced these two numbers is not the traversal's
       business; that is the whole point of the seam. */
    discount = algo->regret_discount(algo, iter);
    if (!is_locked)
    {
        cfr_storage_update_regret_at_street(storage, infoset_key, num_actions, street, regret_delta, discount);

        avg_weight = algo->average_weight(algo, iter, reach[acting_player],
                                          flow_weight, walk->use_flow_focus);
        cfr_storage_update_avg_at_street(storage, infoset_key, num_actions, street, strategy, avg_weight);
    }
    else if (relock_mode)
    {
        /* Periodic relock (FEAT-11). Regret keeps accumulating normally so the
           un-locked actions retain true best-response EVs; on a relock iteration
           the average strategy is snapped back to the locked target and the
           exact EV loss of the forced mix is recorded. */
        cfr_storage_update_regret_at_street(storage, infoset_key, num_actions, street, regret_delta, discount);
        if (relock_iter)
        {
            cfr_storage_overwrite_avg_at_street(storage, infoset_key, num_actions, street, locked);
            /* Exact EV loss (FEAT-11): the acting player is locked only at this
               infoset, so below it plays freely. For each action i we recompute
               the child subtree's recursive best-response value for the acting
               player (opponents follow their average strategy, the acting
               player maximizes at every downstream decision). The loss then
               isolates the cost of the forced mix at THIS node:
                 br_value    = max_i  BR(child_i)
                 forced_value = sum_i locked[i] * BR(child_i)
               both using the same "free below" baseline. Child keys are derived
               fresh here (the descent already released them) and released again
               after the best-response walk to avoid use-after-free. */
            double br_value = -1e300;
            double forced_value = 0.0;
            /* Use the multiway recursive best response whenever the game exposes
               a current_player callback (correct for N players and for 2-player
               games that provide one); otherwise fall back to the 2-player
               variant that derives the opponent as 1 - acting_player. */
            int use_multiway_br = game->current_player ? 1 : 0;
            for (int i = 0; i < num_actions; ++i)
            {
                uint64_t br_child_key = game->apply_action(game, state_key, actions[i], user_data);
                double v;
                if (use_multiway_br)
                {
                    v = cfr_best_response_recursive_multiway(game, storage, acting_player,
                                                         br_child_key, user_data,
                                                         walk->recursion_depth + 1,
                                                         &walk->depth_exceeded,
                                                         walk->telemetry);
                }
                else
                {
                    v = cfr_best_response_recursive(game, storage, acting_player,
                                                1 - acting_player, br_child_key,
                                                user_data, walk->recursion_depth + 1,
                                                &walk->depth_exceeded,
                                                walk->telemetry);
                }
                if (v > br_value)
                    br_value = v;
                forced_value += locked[i] * v;
                if (game->release_state)
                    game->release_state(game, br_child_key, user_data);
            }
            /* Counterfactual reach of the acting player at this infoset, used to
               reach-weight the EV-loss aggregation across all its states. */
            double reach_weight = reach[acting_player];
            if (reach_weight < 0.0)
                reach_weight = 0.0;
            cfr_storage_record_lock_ev_loss(storage, infoset_key, br_value, forced_value, reach_weight);
        }
    }

    if (storage)
        cfr_storage_accumulate_ev_at_street(storage, infoset_key, street, node_util_acting);

    for (int p = 0; p < num_players; ++p)
        out_util[p] = node_util_vec[p];

cfr_exit:
    walk->recursion_depth--;
}
