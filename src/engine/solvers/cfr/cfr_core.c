/*
 * cfr_core.c - Core CFR (Counterfactual Regret Minimization) solver
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * This file implements the core CFR solver logic, which is game-agnostic.
 * It uses a vtable (cfr_game_t) to interact with specific game implementations.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#if !defined(_WIN32)
#include <poker_eval/core/pthread_compat.h>
#endif

#ifdef _WIN32
    #include <malloc.h>  /* For _alloca on Windows */
    #define alloca _alloca
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
    #include <stdlib.h>
#elif defined(__has_include)
    #if __has_include(<alloca.h>)
        #include <alloca.h>
    #else
        #include <stdlib.h>
    #endif
#else
    #include <alloca.h>
#endif

#define CFR_MAX_PLAYERS 8

#if defined(_MSC_VER)
#define CFR_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define CFR_THREAD_LOCAL __thread
#else
#define CFR_THREAD_LOCAL _Thread_local
#endif

// Forward declaration for recursive traversal
static void cfr_traverse_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    const cfr_config_t *config,
    uint64_t state_key,
    double *reach,
    int num_players,
    int iter,
    double *out_util,
    void *user_data);

static CFR_THREAD_LOCAL int g_cfr_current_iter = 0;
static CFR_THREAD_LOCAL int g_cfr_recursion_depth = 0;
static CFR_THREAD_LOCAL int g_cfr_max_depth = 0;
static CFR_THREAD_LOCAL long g_cfr_node_count = 0;
static CFR_THREAD_LOCAL int g_cfr_use_flow_focus = 0;
static CFR_THREAD_LOCAL double g_cfr_flow_pow = 1.0;

struct cfr_metrics_buffer_t
{
    cfr_metrics_snapshot_t *items;
    int capacity;
    int count;
    int head;
#if !defined(_WIN32)
    pthread_mutex_t lock;
#endif
};

static void cfr_metrics_buffer_lock(cfr_metrics_buffer_t *buffer)
{
#if !defined(_WIN32)
    if (buffer)
        pthread_mutex_lock(&buffer->lock);
#else
    (void)buffer;
#endif
}

static void cfr_metrics_buffer_unlock(cfr_metrics_buffer_t *buffer)
{
#if !defined(_WIN32)
    if (buffer)
        pthread_mutex_unlock(&buffer->lock);
#else
    (void)buffer;
#endif
}

typedef struct
{
    double sum;
    double sq_sum;
    uint64_t count;
} cfr_metrics_ev_acc_t;

static void cfr_metrics_buffer_push_internal(cfr_metrics_buffer_t *buffer,
                                             const cfr_metrics_snapshot_t *snapshot,
                                             int history_limit);

static void cfr_metrics_ev_accumulate(uint64_t key,
                                      int n_actions,
                                      const double *regret,
                                      const double *avg_strategy,
                                      double ev_sum,
                                      double ev_sq_sum,
                                      uint64_t sample_count,
                                      void *user)
{
    (void)key;
    (void)n_actions;
    (void)regret;
    (void)avg_strategy;
    cfr_metrics_ev_acc_t *acc = (cfr_metrics_ev_acc_t *)user;
    acc->sum += ev_sum;
    acc->sq_sum += ev_sq_sum;
    acc->count += sample_count;
}

double cfr_solve(
    cfr_game_t *game,
    cfr_storage_t *storage,
    const cfr_config_t *config,
    double *out_exploitability)
{
    if (!game || !storage || !config)
    {
        if (out_exploitability)
            *out_exploitability = -1.0;
        return -1.0;
    }

    cfr_storage_set_strategy_mode(config->enable_ecfr, config->ecfr_lambda);
    g_cfr_use_flow_focus = config->enable_mccfvfp ? 1 : 0;
    g_cfr_flow_pow = (fabs(config->mccfvfp_flow_pow) > 1e-9) ? config->mccfvfp_flow_pow : 1.0;

    clock_t start_clock = clock();
    int progress_interval = config->progress_interval;
    if (progress_interval < 0)
        progress_interval = 0;

    int start_iter = 0;
    if (config->resume_path && *config->resume_path)
    {
        uint64_t saved_iter = 0;
        if (cfr_storage_load_checkpoint(storage, config->resume_path, &saved_iter) == 0)
        {
            if (saved_iter > (uint64_t)config->max_iterations)
                saved_iter = (uint64_t)config->max_iterations;
            start_iter = (int)saved_iter;
            if (start_iter > 0)
            {
                fprintf(stderr, "[cfr] resumed from %s at iteration %d\n", config->resume_path, start_iter);
            }
        }
        else
        {
            fprintf(stderr, "[cfr] warning: failed to resume from %s (%s)\n", config->resume_path, strerror(errno));
        }
    }

    int last_iter = start_iter;
    int aborted = 0;
    long long metrics_nodes_total = 0;
    double metrics_elapsed_total = 0.0;
    int metrics_interval_raw = config->metrics_interval;
    if (metrics_interval_raw <= 0)
        metrics_interval_raw = 1;
    for (int it = start_iter; it < config->max_iterations; ++it)
    {
        if (config->stop_flag && *config->stop_flag)
        {
            aborted = 1;
            break;
        }
        clock_t iter_start_clock = clock();
        if (config->trace_iterations)
        {
            fprintf(stderr, "[cfr] iter %d/%d started\n", it + 1, config->max_iterations);
            fflush(stderr);
        }
        g_cfr_current_iter = it + 1;
        g_cfr_recursion_depth = 0;
        g_cfr_max_depth = 0;
        g_cfr_node_count = 0;
        int num_players = (game->num_players > 0) ? game->num_players : 2;
        if (num_players > CFR_MAX_PLAYERS)
        {
            fprintf(stderr, "[cfr] error: num_players=%d exceeds max supported (%d)\n",
                    num_players, CFR_MAX_PLAYERS);
            return -1.0;
        }

        double *reach = (double *)alloca(sizeof(double) * (size_t)num_players);
        double *util = (double *)alloca(sizeof(double) * (size_t)num_players);
        for (int rp = 0; rp < num_players; ++rp)
            reach[rp] = 1.0;

        uint64_t root_key = (uint64_t)(game->initial_state);
        if (!root_key && game->initial_state)
            root_key = (uint64_t)(uintptr_t)(game->initial_state);

        if (game->traverse && num_players == 2)
        {
            /* Legacy two-player traverse */
            game->traverse(game, storage, root_key, 0, 1.0, 1.0, game->game_data);
        }
        else
        {
            cfr_traverse_recursive(game, storage, config, root_key, reach, num_players, it, util, NULL);
        }

        if (config->stop_flag && *config->stop_flag)
        {
            aborted = 1;
            break;
        }

        double iter_elapsed = (double)(clock() - iter_start_clock) / CLOCKS_PER_SEC;
        metrics_elapsed_total += iter_elapsed;
        metrics_nodes_total += g_cfr_node_count;

        if (config->trace_iterations)
        {
            fprintf(stderr, "[cfr] iter %d/%d finished in %.3fs (nodes=%ld, max_depth=%d)\n",
                    it + 1, config->max_iterations, iter_elapsed, g_cfr_node_count, g_cfr_max_depth);
            fflush(stderr);
        }

        if ((config->metrics_fn || config->metrics_buffer))
        {
            if (((it + 1) % metrics_interval_raw) == 0)
            {
                cfr_metrics_snapshot_t snap;
                memset(&snap, 0, sizeof(snap));
                snap.iteration = it + 1;
                snap.elapsed_sec = metrics_elapsed_total;
                snap.iteration_time_sec = iter_elapsed;
                snap.nodes_iteration = g_cfr_node_count;
                snap.nodes_total = metrics_nodes_total;
                snap.nodes_per_sec = (iter_elapsed > 0.0) ? ((double)g_cfr_node_count / iter_elapsed) : 0.0;
                snap.iterations_per_sec = (metrics_elapsed_total > 0.0) ? ((double)(it + 1) / metrics_elapsed_total) : 0.0;
                snap.infosets_total = cfr_storage_count_infosets(storage);
                snap.num_players = (game->num_players > 0) ? game->num_players : 0;

                if (config->metrics_level >= 1)
                    snap.exploitability = cfr_exploitability_proxy(game, storage, NULL);

                if (config->metrics_level >= 2)
                {
                    cfr_metrics_ev_acc_t acc = {0.0, 0.0, 0};
                    cfr_storage_iterate_stats(storage, cfr_metrics_ev_accumulate, &acc);
                    if (acc.count > 0)
                    {
                        double mean = acc.sum / (double)acc.count;
                        double variance = (acc.sq_sum / (double)acc.count) - mean * mean;
                        if (variance < 0.0)
                            variance = 0.0;
                        snap.ev_mean = mean;
                        snap.ev_stddev = sqrt(variance);
                        snap.volatility = snap.ev_stddev;
                        double scale = (fabs(config->metrics_mchips_scale) > 1e-9) ? config->metrics_mchips_scale : 1000.0;
                        snap.mchips_per_sec = mean * scale;
                        if (config->metrics_bb_value > 0.0)
                            snap.bb_per_100 = (mean / config->metrics_bb_value) * 100.0;
                    }
                }

                if (config->metrics_buffer)
                    cfr_metrics_buffer_push_internal(config->metrics_buffer, &snap, config->metrics_history);
                if (config->metrics_fn)
                    config->metrics_fn(&snap, config->metrics_user);
            }
        }

        if (config->monitor_fn && config->monitor_period > 0 && ((it + 1) % config->monitor_period) == 0)
        {
            config->monitor_fn(it + 1, game, storage, config->monitor_user);
        }

        if (progress_interval > 0 && (it + 1) % progress_interval == 0)
        {
            double elapsed = (double)(clock() - start_clock) / CLOCKS_PER_SEC;
            double progress = (it + 1) / (double)config->max_iterations;
            double est_total = (progress > 0.0) ? (elapsed / progress) : 0.0;
            double eta = est_total - elapsed;
            if (eta < 0.0)
                eta = 0.0;
            fprintf(stderr, "[cfr] iter %d/%d  elapsed %.2fs  est %.2fs  eta %.2fs\n",
                    it + 1, config->max_iterations, elapsed, est_total, eta);
            fflush(stderr);
            if (config->monitor_fn && config->monitor_period == 0)
                config->monitor_fn(it + 1, game, storage, config->monitor_user);
        }

        if (config->checkpoint_path && config->checkpoint_interval > 0 && (it + 1) % config->checkpoint_interval == 0)
        {
            if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)(it + 1)) != 0)
            {
                fprintf(stderr, "[cfr] warning: failed to write checkpoint %s (%s)\n",
                        config->checkpoint_path, strerror(errno));
            }
        }

        if (config->checkpoint_interval > 0 && (it + 1) % config->checkpoint_interval == 0)
        {
            double exploitability = cfr_exploitability_proxy(game, storage, NULL);
            if (out_exploitability)
            {
                *out_exploitability = exploitability;
            }
            if (exploitability < config->convergence_threshold)
            {
                break;
            }
        }
        last_iter = it + 1;
        if (config->stop_flag && *config->stop_flag)
        {
            aborted = 1;
            break;
        }
    }

    double final_exploitability = cfr_exploitability_proxy(game, storage, NULL);
    if (out_exploitability)
    {
        *out_exploitability = final_exploitability;
    }

    if (progress_interval > 0 && config->max_iterations > 0)
    {
        double total_elapsed = (double)(clock() - start_clock) / CLOCKS_PER_SEC;
        fprintf(stderr, "[cfr] iter %d/%d  elapsed %.2fs  est %.2fs  eta 0.00s\n",
                config->max_iterations, config->max_iterations, total_elapsed, total_elapsed);
        fflush(stderr);
    }

    cfr_storage_set_strategy_mode(0, 1.0);
    g_cfr_use_flow_focus = 0;
    g_cfr_flow_pow = 1.0;

    if (config->checkpoint_path && config->checkpoint_final)
    {
        if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)config->max_iterations) != 0)
        {
            fprintf(stderr, "[cfr] warning: failed to write final checkpoint %s (%s)\n",
                    config->checkpoint_path, strerror(errno));
        }
    }
    else if (config->checkpoint_path && config->stop_flag && *config->stop_flag)
    {
        if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)last_iter) != 0)
        {
            fprintf(stderr, "[cfr] warning: failed to write checkpoint %s (%s)\n",
                    config->checkpoint_path, strerror(errno));
        }
    }

    if (config->monitor_fn && config->monitor_period == 0)
        config->monitor_fn(last_iter, game, storage, config->monitor_user);

    if (aborted)
        fprintf(stderr, "[cfr] stopped at iteration %d\n", last_iter);

    return final_exploitability;
}

static void cfr_traverse_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    const cfr_config_t *config,
    uint64_t state_key,
    double *reach,
    int num_players,
    int iter,
    double *out_util,
    void *user_data)
{
    int actions[16];
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

    for (int p = 0; p < num_players; ++p)
        out_util[p] = 0.0;

    if (config->stop_flag && *config->stop_flag)
        return;

    g_cfr_recursion_depth++;
    if (g_cfr_recursion_depth > g_cfr_max_depth)
    {
        g_cfr_max_depth = g_cfr_recursion_depth;
        if (config->trace_iterations)
        {
            fprintf(stderr, "[cfr] iter %d depth -> %d (state=0x%llx)\n",
                    g_cfr_current_iter, g_cfr_max_depth, (unsigned long long)state_key);
            fflush(stderr);
        }
    }
    g_cfr_node_count++;

    if (game->is_terminal(game, state_key, user_data))
    {
        for (int p = 0; p < num_players; ++p)
            out_util[p] = game->get_utility(game, state_key, p, user_data);
        if (storage)
            cfr_storage_accumulate_ev(storage, state_key, out_util[0]);
        goto cfr_exit;
    }

    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        /* Chance node: propagate the expected utility across all equally
         * likely runouts. No regrets or strategies are tracked here. */
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0)
            goto cfr_exit;
        for (int c = 0; c < outcomes; ++c)
        {
            uint64_t child_key = game->apply_chance(game, state_key, c, user_data);
            cfr_traverse_recursive(
                game, storage, config, child_key,
                reach, num_players, iter, child_util, user_data);
            for (int p = 0; p < num_players; ++p)
                out_util[p] += child_util[p] / (double)outcomes;
        }
        goto cfr_exit;
    }

    num_actions = game->get_actions(game, state_key, actions, 16, user_data);
    if (num_actions == 0)
    {
        for (int p = 0; p < num_players; ++p)
            out_util[p] = 0.0;
        goto cfr_exit;
    }

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
        fprintf(stderr, "[cfr] iter %d depth %d state 0x%llx actions=%d player=%d\n",
                g_cfr_current_iter, g_cfr_recursion_depth, (unsigned long long)state_key, num_actions, acting_player);
        fflush(stderr);
    }

    strategy = (double *)alloca(sizeof(double) * (size_t)num_actions);
    cfr_storage_get_strategy(storage, state_key, num_actions, strategy);

    regret_delta = (double *)alloca(sizeof(double) * (size_t)num_actions);
    action_util = (double *)alloca(sizeof(double) * (size_t)num_actions);
    for (int p = 0; p < num_players; ++p)
        node_util_vec[p] = 0.0;


    for (int i = 0; i < num_actions; ++i)
    {
        uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
        if (config->trace_iterations)
        {
            fprintf(stderr, "[cfr] iter %d depth %d action %d -> state 0x%llx\n",
                    g_cfr_current_iter, g_cfr_recursion_depth, actions[i], (unsigned long long)next_state_key);
            fflush(stderr);
        }

        for (int p = 0; p < num_players; ++p)
            next_reach[p] = reach[p];
        next_reach[acting_player] *= strategy[i];

        cfr_traverse_recursive(
            game, storage, config, next_state_key,
            next_reach, num_players, iter, child_util, user_data);

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

    if (g_cfr_use_flow_focus)
    {
        double flow = 1.0;
        for (int p = 0; p < num_players; ++p)
            flow *= reach[p];
        if (flow < 1e-12)
            flow = 1e-12;
        if (fabs(g_cfr_flow_pow - 1.0) > 1e-9)
            flow = pow(flow, g_cfr_flow_pow);
        flow_weight = flow;
    }

    for (int i = 0; i < num_actions; ++i)
        regret_delta[i] = (action_util[i] - node_util_acting) * reach_others * flow_weight;

    t = (double)(iter + 1);
    if (config->enable_dcfr)
    {
        discount *= (t > 0.0) ? (pow(t, config->dcfr_alpha) / (pow(t, config->dcfr_alpha) + 1.0)) : 1.0;
    }
    cfr_storage_update_regret(storage, state_key, num_actions, regret_delta, discount);

    avg_weight = reach[acting_player];
    if (g_cfr_use_flow_focus)
        avg_weight *= flow_weight;
    if (config->enable_linear_avg)
        avg_weight *= t;
    if (config->enable_dcfr)
    {
        if (fabs(config->dcfr_beta) > 1e-9)
            avg_weight *= pow(t, config->dcfr_beta);
        if (fabs(config->dcfr_gamma) > 1e-9)
            avg_weight *= pow(t, config->dcfr_gamma);
    }
    cfr_storage_update_avg(storage, state_key, num_actions, strategy, avg_weight);

    if (storage)
        cfr_storage_accumulate_ev(storage, state_key, node_util_acting);

    for (int p = 0; p < num_players; ++p)
        out_util[p] = node_util_vec[p];

cfr_exit:
    g_cfr_recursion_depth--;
}

static double best_response_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    int current_player,
    uint64_t state_key,
    void *user_data)
{
    if (game->is_terminal(game, state_key, user_data))
    {
        return game->get_utility(game, state_key, br_player, user_data);
    }

    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0)
            return 0.0;
        double chance_value = 0.0;
        for (int i = 0; i < outcomes; ++i)
        {
            uint64_t child_key = game->apply_chance(game, state_key, i, user_data);
            /* A dealt child starts a fresh street: the acting player may no
             * longer match the parity from the pre-deal action sequence. */
            int child_cp = 1 - current_player;
            if (game->current_player)
                child_cp = game->current_player(game, child_key, user_data);
            chance_value += best_response_recursive(game, storage, br_player, child_cp, child_key, user_data) / (double)outcomes;
        }
        return chance_value;
    }

    int actions[16];
    int num_actions = game->get_actions(game, state_key, actions, 16, user_data);
    if (num_actions == 0)
        return 0.0;

    if (current_player == br_player)
    {
        double best_value = (br_player == 0) ? -1e100 : 1e100;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            double value = best_response_recursive(game, storage, br_player, 1 - current_player, next_state_key, user_data);
            if (br_player == 0)
            {
                if (value > best_value)
                    best_value = value;
            }
            else
            {
                if (value < best_value)
                    best_value = value;
            }
        }
        return best_value;
    }
    else
    {
        double *avg_strategy = (double *)alloca(sizeof(double) * num_actions);
        cfr_storage_get_avg_strategy(storage, state_key, num_actions, avg_strategy);
        double node_value = 0.0;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            node_value += avg_strategy[i] * best_response_recursive(game, storage, br_player, 1 - current_player, next_state_key, user_data);
        }
        return node_value;
    }
}

double cfr_best_response_value(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data)
{
    if (game->num_players && game->num_players != 2)
        return 0.0;
    uint64_t root_key = (uint64_t)(game->initial_state);
    if (!root_key && game->initial_state)
    {
        root_key = (uint64_t)(uintptr_t)(game->initial_state);
    }
    return best_response_recursive(game, storage, player, 0, root_key, user_data);
}

cfr_metrics_buffer_t *cfr_metrics_buffer_create(int capacity)
{
    if (capacity < 0)
        capacity = 0;
    cfr_metrics_buffer_t *buffer = (cfr_metrics_buffer_t *)calloc(1, sizeof(cfr_metrics_buffer_t));
    if (!buffer)
        return NULL;
#if !defined(_WIN32)
    if (pthread_mutex_init(&buffer->lock, NULL) != 0)
    {
        free(buffer);
        return NULL;
    }
#endif
    buffer->capacity = capacity;
    buffer->head = -1;
    if (capacity > 0)
    {
        buffer->items = (cfr_metrics_snapshot_t *)calloc((size_t)capacity, sizeof(cfr_metrics_snapshot_t));
        if (!buffer->items)
        {
#if !defined(_WIN32)
            pthread_mutex_destroy(&buffer->lock);
#endif
            free(buffer);
            return NULL;
        }
    }
    return buffer;
}

void cfr_metrics_buffer_destroy(cfr_metrics_buffer_t *buffer)
{
    if (!buffer)
        return;
#if !defined(_WIN32)
    pthread_mutex_destroy(&buffer->lock);
#endif
    free(buffer->items);
    free(buffer);
}

void cfr_metrics_buffer_clear(cfr_metrics_buffer_t *buffer)
{
    if (!buffer)
        return;
    cfr_metrics_buffer_lock(buffer);
    buffer->count = 0;
    buffer->head = -1;
    cfr_metrics_buffer_unlock(buffer);
}

int cfr_metrics_buffer_count(cfr_metrics_buffer_t *buffer)
{
    if (!buffer)
        return 0;
    cfr_metrics_buffer_lock(buffer);
    int count = buffer->count;
    cfr_metrics_buffer_unlock(buffer);
    return count;
}

int cfr_metrics_buffer_get_latest(cfr_metrics_buffer_t *buffer, cfr_metrics_snapshot_t *out_snapshot)
{
    if (!buffer || buffer->count == 0 || !out_snapshot)
        return -1;
    if (buffer->head < 0)
        return -1;
    cfr_metrics_buffer_lock(buffer);
    if (buffer->head < 0 || buffer->count == 0)
    {
        cfr_metrics_buffer_unlock(buffer);
        return -1;
    }
    *out_snapshot = buffer->items[buffer->head];
    cfr_metrics_buffer_unlock(buffer);
    return 0;
}

int cfr_metrics_buffer_get(cfr_metrics_buffer_t *buffer, int index_from_newest, cfr_metrics_snapshot_t *out_snapshot)
{
    if (!buffer || !out_snapshot || index_from_newest < 0 || index_from_newest >= buffer->count || buffer->capacity <= 0)
        return -1;
    if (buffer->head < 0)
        return -1;
    cfr_metrics_buffer_lock(buffer);
    int idx = buffer->head - index_from_newest;
    while (idx < 0)
        idx += buffer->capacity;
    *out_snapshot = buffer->items[idx % buffer->capacity];
    cfr_metrics_buffer_unlock(buffer);
    return 0;
}

static void cfr_metrics_buffer_push_internal(cfr_metrics_buffer_t *buffer,
                                             const cfr_metrics_snapshot_t *snapshot,
                                             int history_limit)
{
    if (!buffer || !snapshot || buffer->capacity <= 0)
        return;
    cfr_metrics_buffer_lock(buffer);
    if (buffer->head < 0)
        buffer->head = 0;
    else
        buffer->head = (buffer->head + 1) % buffer->capacity;
    buffer->items[buffer->head] = *snapshot;
    if (buffer->count < buffer->capacity)
        buffer->count++;
    int effective_limit = history_limit;
    if (effective_limit <= 0 || effective_limit > buffer->capacity)
        effective_limit = buffer->capacity;
   if (buffer->count > effective_limit)
        buffer->count = effective_limit;
    cfr_metrics_buffer_unlock(buffer);
}

double cfr_exploitability_proxy(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data)
{
    if (game->num_players && game->num_players != 2)
        return 0.0;
    double br_p0 = cfr_best_response_value(game, storage, 0, user_data);
    double br_p1 = cfr_best_response_value(game, storage, 1, user_data);
    // Exploitability is the sum of best response values for a zero-sum game
    return br_p0 + br_p1;
}

/* ===== Multiway Best-Response and Exploitability ===== */

/**
 * Internal recursive best-response for N-player games.
 * The BR player maximizes their value; all others follow avg strategy from storage.
 */
static double best_response_recursive_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    uint64_t state_key,
    void *user_data)
{
    /* Terminal state - return utility for BR player */
    if (game->is_terminal(game, state_key, user_data))
    {
        return game->get_utility(game, state_key, br_player, user_data);
    }

    /* Chance node - average over runouts */
    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0)
            return 0.0;
        double chance_value = 0.0;
        for (int i = 0; i < outcomes; ++i)
        {
            uint64_t child_key = game->apply_chance(game, state_key, i, user_data);
            chance_value += best_response_recursive_multiway(game, storage, br_player, child_key, user_data) / (double)outcomes;
        }
        return chance_value;
    }

    /* Get current player (requires current_player callback) */
    int current_player = -1;
    if (game->current_player)
    {
        current_player = game->current_player(game, state_key, user_data);
    }
    else
    {
        /* Fallback: cannot determine current player, abort */
        return 0.0;
    }

    int actions[32];
    int num_actions = game->get_actions(game, state_key, actions, 32, user_data);
    if (num_actions == 0)
        return 0.0;

    if (current_player == br_player)
    {
        /* BR player: maximize expected value */
        double best_value = -1e100;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            double value = best_response_recursive_multiway(game, storage, br_player, next_state_key, user_data);
            if (value > best_value)
                best_value = value;
        }
        return best_value;
    }
    else
    {
        /* Other player: follow average strategy */
        double *avg_strategy = (double *)alloca(sizeof(double) * num_actions);
        cfr_storage_get_avg_strategy(storage, state_key, num_actions, avg_strategy);
        
        double node_value = 0.0;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            node_value += avg_strategy[i] * best_response_recursive_multiway(game, storage, br_player, next_state_key, user_data);
        }
        return node_value;
    }
}

double cfr_best_response_value_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data)
{
    if (!game || !storage)
        return 0.0;
    
    /* Must have current_player callback for multiway */
    if (!game->current_player)
    {
        /* Fall back to 2-player version if available */
        if (game->num_players == 2)
            return cfr_best_response_value(game, storage, player, user_data);
        return 0.0;
    }
    
    int num_players = game->num_players > 0 ? game->num_players : 2;
    if (player < 0 || player >= num_players)
        return 0.0;

    uint64_t root_key = (uint64_t)(game->initial_state);
    if (!root_key && game->initial_state)
    {
        root_key = (uint64_t)(uintptr_t)(game->initial_state);
    }
    
    return best_response_recursive_multiway(game, storage, player, root_key, user_data);
}

int cfr_exploitability_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data,
    cfr_exploitability_result_t *out_result)
{
    if (!game || !storage || !out_result)
        return -1;
    
    memset(out_result, 0, sizeof(*out_result));
    
    int num_players = game->num_players > 0 ? game->num_players : 2;
    out_result->num_players = num_players;
    
    /* Compute policy values (EV under average strategy) */
    cfr_policy_value_result_t policy_result;
    if (cfr_compute_policy_values_detailed(game, storage, user_data, &policy_result) != 0)
        return -1;
    
    /* Compute best-response values for each player */
    double total_exploit = 0.0;
    for (int p = 0; p < num_players; ++p)
    {
        out_result->policy_value[p] = policy_result.ev[p];
        out_result->br_value[p] = cfr_best_response_value_multiway(game, storage, p, user_data);
        
        /* Exploitability = BR_value - policy_value */
        /* For a Nash equilibrium, BR should equal policy value (no incentive to deviate) */
        out_result->exploitability[p] = out_result->br_value[p] - out_result->policy_value[p];
        total_exploit += out_result->exploitability[p];
    }
    
    out_result->total_exploitability = total_exploit;
    
    /* Nash distance estimate: sqrt(sum of squared exploitabilities) / num_players */
    double sq_sum = 0.0;
    for (int p = 0; p < num_players; ++p)
    {
        sq_sum += out_result->exploitability[p] * out_result->exploitability[p];
    }
    out_result->nash_distance = sqrt(sq_sum) / num_players;
    
    return 0;
}

void cfr_exploitability_print(const cfr_exploitability_result_t *result)
{
    if (!result)
    {
        printf("Exploitability result: NULL\n");
        return;
    }
    
    printf("=== Exploitability Analysis (%d players) ===\n", result->num_players);
    for (int p = 0; p < result->num_players; ++p)
    {
        printf("Player %d: policy_EV=%.6f  BR_EV=%.6f  exploit=%.6f\n",
               p, result->policy_value[p], result->br_value[p], result->exploitability[p]);
    }
    printf("Total exploitability: %.6f\n", result->total_exploitability);
    printf("Nash distance estimate: %.6f\n", result->nash_distance);
}

/* ===== Policy Value Computation ===== */

/**
 * Internal context for policy value traversal
 */
typedef struct {
    cfr_game_t *game;
    cfr_storage_t *storage;
    int num_players;
    size_t nodes_visited;
    double reach_sum;
    double ev_sum[CFR_MAX_PLAYERS];
    double ev_sq_sum[CFR_MAX_PLAYERS];  /* For variance calculation */
    void *user_data;
} policy_value_ctx_t;

/**
 * Recursive policy value computation
 *
 * Traverses the game tree using the average strategy from storage,
 * computing expected values for all players simultaneously.
 *
 * @param ctx        Traversal context
 * @param state_key  Current state key
 * @param reach      Array of reach probabilities per player
 * @param out_util   Output: utility values per player at this node
 */
static void policy_value_recursive(
    policy_value_ctx_t *ctx,
    uint64_t state_key,
    const double *reach,
    double *out_util)
{
    ctx->nodes_visited++;

    /* Initialize output */
    for (int p = 0; p < ctx->num_players; ++p)
        out_util[p] = 0.0;

    /* Check terminal state */
    if (ctx->game->is_terminal(ctx->game, state_key, ctx->user_data))
    {
        /* Compute reach probability product */
        double reach_prod = 1.0;
        for (int p = 0; p < ctx->num_players; ++p)
            reach_prod *= reach[p];

        /* Accumulate terminal utilities */
        for (int p = 0; p < ctx->num_players; ++p)
        {
            double util = ctx->game->get_utility(ctx->game, state_key, p, ctx->user_data);
            out_util[p] = util;

            /* Weighted accumulation for expected value */
            ctx->ev_sum[p] += reach_prod * util;
            ctx->ev_sq_sum[p] += reach_prod * util * util;
        }
        ctx->reach_sum += reach_prod;
        return;
    }

    /* Chance node - uniform average over runouts */
    if (ctx->game->is_chance && ctx->game->is_chance(ctx->game, state_key, ctx->user_data))
    {
        int outcomes = ctx->game->get_chance_outcomes
            ? ctx->game->get_chance_outcomes(ctx->game, state_key, ctx->user_data)
            : 0;
        if (outcomes <= 0)
            return;
        double child_util[CFR_MAX_PLAYERS];
        for (int c = 0; c < outcomes; ++c)
        {
            uint64_t child_key = ctx->game->apply_chance(ctx->game, state_key, c, ctx->user_data);
            policy_value_recursive(ctx, child_key, reach, child_util);
            for (int p = 0; p < ctx->num_players; ++p)
                out_util[p] += child_util[p] / (double)outcomes;
        }
        return;
    }

    /* Get available actions */
    int actions[16];
    int num_actions = ctx->game->get_actions(ctx->game, state_key, actions, 16, ctx->user_data);
    if (num_actions == 0)
        return;

    /* Determine acting player */
    int acting_player = 0;
    if (ctx->game->current_player)
    {
        acting_player = ctx->game->current_player(ctx->game, state_key, ctx->user_data);
        if (acting_player < 0 || acting_player >= ctx->num_players)
            acting_player = 0;
    }

    /* Get average strategy for this infoset */
    double *avg_strategy = (double *)alloca(sizeof(double) * (size_t)num_actions);
    cfr_storage_get_avg_strategy(ctx->storage, state_key, num_actions, avg_strategy);

    /* Traverse children with weighted reach */
    double child_util[CFR_MAX_PLAYERS];
    double next_reach[CFR_MAX_PLAYERS];

    for (int a = 0; a < num_actions; ++a)
    {
        /* Skip actions with zero probability */
        if (avg_strategy[a] < 1e-12)
            continue;

        /* Update reach probabilities */
        for (int p = 0; p < ctx->num_players; ++p)
            next_reach[p] = reach[p];
        next_reach[acting_player] *= avg_strategy[a];

        /* Recurse */
        uint64_t next_state = ctx->game->apply_action(ctx->game, state_key, actions[a], ctx->user_data);
        policy_value_recursive(ctx, next_state, next_reach, child_util);

        /* Accumulate expected utility */
        for (int p = 0; p < ctx->num_players; ++p)
            out_util[p] += avg_strategy[a] * child_util[p];
    }
}

double cfr_compute_policy_value(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data)
{
    if (!game || !storage)
        return 0.0;

    int num_players = (game->num_players > 0) ? game->num_players : 2;
    if (player < 0 || player >= num_players)
        return 0.0;

    /* Initialize context */
    policy_value_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.game = game;
    ctx.storage = storage;
    ctx.num_players = num_players;
    ctx.user_data = user_data;

    /* Get root state */
    uint64_t root_key = (uint64_t)(game->initial_state);
    if (!root_key && game->initial_state)
        root_key = (uint64_t)(uintptr_t)(game->initial_state);

    /* Initialize reach probabilities */
    double reach[CFR_MAX_PLAYERS];
    double util[CFR_MAX_PLAYERS];
    for (int p = 0; p < num_players; ++p)
        reach[p] = 1.0;

    /* Traverse */
    policy_value_recursive(&ctx, root_key, reach, util);

    /* Return EV for requested player */
    return util[player];
}

int cfr_compute_policy_values_detailed(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data,
    cfr_policy_value_result_t *out_result)
{
    if (!game || !storage || !out_result)
        return -1;

    int num_players = (game->num_players > 0) ? game->num_players : 2;
    if (num_players > CFR_MAX_PLAYERS)
        return -1;

    /* Initialize result */
    memset(out_result, 0, sizeof(*out_result));
    out_result->num_players = num_players;

    /* Initialize context */
    policy_value_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.game = game;
    ctx.storage = storage;
    ctx.num_players = num_players;
    ctx.user_data = user_data;

    /* Get root state */
    uint64_t root_key = (uint64_t)(game->initial_state);
    if (!root_key && game->initial_state)
        root_key = (uint64_t)(uintptr_t)(game->initial_state);

    /* Initialize reach probabilities */
    double reach[CFR_MAX_PLAYERS];
    double util[CFR_MAX_PLAYERS];
    for (int p = 0; p < num_players; ++p)
        reach[p] = 1.0;

    /* Traverse */
    policy_value_recursive(&ctx, root_key, reach, util);

    /* Copy results */
    out_result->nodes_visited = ctx.nodes_visited;
    out_result->reach_sum = ctx.reach_sum;

    for (int p = 0; p < num_players; ++p)
    {
        out_result->ev[p] = util[p];

        /* Compute variance: Var(X) = E[X^2] - E[X]^2 */
        if (ctx.reach_sum > 0.0)
        {
            double mean = ctx.ev_sum[p] / ctx.reach_sum;
            double mean_sq = ctx.ev_sq_sum[p] / ctx.reach_sum;
            double variance = mean_sq - mean * mean;
            if (variance < 0.0)
                variance = 0.0;
            out_result->variance[p] = variance;
            out_result->std_dev[p] = sqrt(variance);
        }
    }

    return 0;
}
