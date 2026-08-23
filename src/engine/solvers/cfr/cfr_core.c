/*
 * cfr_core.c - Core CFR (Counterfactual Regret Minimization) solver
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * This file implements the core CFR solver logic, which is game-agnostic.
 * It uses a vtable (cfr_game_t) to interact with specific game implementations.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/solver/pe_telemetry.h>

#include "cfr_traversal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <float.h>
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

int cfr_config_to_pe_solver_config(const cfr_config_t *legacy,
                                   pe_solver_config_t *out)
{
    if (legacy == NULL || out == NULL)
        return -1;

    memset(out, 0, sizeof(*out));

    /* The legacy engine is the scalar lane. Its flow-focus switch is a
       traversal weighting detail, not a separate v3 traversal mode. */
    out->algorithm.preset = PE_PRESET_CUSTOM;
    out->algorithm.traversal = PE_TRAVERSAL_FULL_SCALAR;
    out->algorithm.pruning = PE_PRUNE_NONE;
    out->algorithm.dcfr_alpha = legacy->dcfr_alpha;
    out->algorithm.dcfr_beta = legacy->dcfr_beta;
    out->algorithm.dcfr_gamma = legacy->dcfr_gamma;
    out->algorithm.exponential_lambda = legacy->ecfr_lambda > 0.0
                                             ? legacy->ecfr_lambda : 1.0;
    out->algorithm.outcome_epsilon = 0.6;

    if (legacy->enable_ecfr)
    {
        out->algorithm.regret = PE_REGRET_LEGACY_EXP;
        out->algorithm.policy = PE_POLICY_EXPONENTIAL;
    }
    else if (legacy->enable_dcfr)
    {
        out->algorithm.regret = PE_REGRET_DCFR;
        out->algorithm.policy = PE_POLICY_REGRET_MATCHING;
    }
    else
    {
        out->algorithm.regret = PE_REGRET_VANILLA;
        out->algorithm.policy = PE_POLICY_REGRET_MATCHING;
    }

    if (legacy->enable_dcfr)
        out->algorithm.averaging = PE_AVG_POWER;
    else if (legacy->enable_linear_avg)
        out->algorithm.averaging = PE_AVG_LINEAR;
    else
        out->algorithm.averaging = PE_AVG_UNIFORM;

    out->execution.backend = PE_COMPUTE_CPU_REF;
    out->execution.stages.traversal = PE_COMPUTE_CPU_REF;
    out->execution.stages.update = PE_COMPUTE_CPU_REF;
    out->execution.stages.terminal_eval = PE_COMPUTE_CPU_REF;
    out->execution.precision = PE_PREC_F64;
    out->execution.device_id = -1;
    out->execution.cpu_threads = 1;
    out->execution.deterministic = 1;

    out->seed = (uint64_t)(uint32_t)legacy->seed;
    out->max_iterations = legacy->max_iterations > 0
                              ? (uint64_t)legacy->max_iterations : 0u;
    out->target_exploitability_mbb = legacy->convergence_threshold > 0.0
                                         ? legacy->convergence_threshold : 0.0;
    out->exploitability_interval = legacy->exploitability_interval > 0
                                       ? (uint64_t)legacy->exploitability_interval : 0u;
    return 0;
}

/* FEAT-14 (#150): non-uniform chance deals. Returns the weight of chance
 * outcome index `outcome` at the given state, defaulting to 1.0 when the game
 * exposes no get_chance_weight callback (equally-likely outcomes). Negative
 * or NaN weights are clamped to 0 so a misbehaving callback can never poison
 * the chance average. */
double cfr_chance_weight(cfr_game_t *game, uint64_t state_key, int outcome,
                         void *user_data)
{
    if (!game || !game->get_chance_weight)
        return 1.0;
    double w = game->get_chance_weight(game, state_key, outcome, user_data);
    if (!(w > 0.0))
        return 0.0;
    return w;
}

int pe_cfr_set_utility_function(cfr_game_t *game, pe_cfr_utility_config_t utility_config)
{
    if (!game)
        return -1;
    game->utility = utility_config;
    return 0;
}


void cfr_walk_ctx_init(cfr_walk_ctx_t *walk)
{
    walk->current_iter = 0;
    walk->recursion_depth = 0;
    walk->max_depth = 0;
    walk->depth_exceeded = 0;
    walk->node_count = 0;
    walk->use_flow_focus = 0;
    walk->flow_pow = 1.0;
    walk->telemetry = pe_telemetry_stderr();
}


// Forward declarations: recursive best-response values used by the periodic
// relock EV-loss measurement (FEAT-11).
double cfr_best_response_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    int current_player,
    uint64_t state_key,
    void *user_data,
    int depth,
    int *depth_exceeded,
    const pe_telemetry_ops_t *telemetry);

double cfr_best_response_recursive_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    uint64_t state_key,
    void *user_data,
    int depth,
    int *depth_exceeded,
    const pe_telemetry_ops_t *telemetry);


uint64_t cfr_traversal_storage_key(cfr_game_t *game, uint64_t state_key)
{
    if (game->get_infoset_key)
        return game->get_infoset_key((const void *)(uintptr_t)state_key);
    return state_key;
}

int cfr_traversal_storage_street(cfr_game_t *game, uint64_t state_key)
{
    return game->get_street ? game->get_street(game, state_key, game->game_data) : -1;
}

/* ============================================================================
 * Terminal utility evaluation (ISSUE-14, #170)
 *
 * Every terminal utility read in the solver flows through these helpers.  When a
 * pe_utility_fn is configured on the game AND the game provides get_final_stacks,
 * the final stack vector is derived once (memoized for the call) and each
 * player's payoff is produced by utility_fn, which keeps expensive non-linear
 * evaluations (ICM, rake, risk adjustments) stable and avoids re-deriving stacks
 * once per player.  Otherwise the helper performs the exact legacy
 * get_utility-per-player loop, so default linear-chip behaviour is preserved.
 * ============================================================================ */
void cfr_traversal_terminal_utilities(cfr_game_t *game, uint64_t state_key,
                                   int num_players, double *out,
                                   void *user_data)
{
    if (num_players > CFR_MAX_PLAYERS)
        num_players = CFR_MAX_PLAYERS;
    if (game && game->utility.utility_fn && game->get_final_stacks)
    {
        int32_t stacks[CFR_MAX_PLAYERS];
        if (game->get_final_stacks(game, state_key, stacks, user_data) == 0)
        {
            for (int p = 0; p < num_players; ++p)
                out[p] = game->utility.utility_fn(stacks, num_players, p,
                                                  game->utility.user_data);
            return;
        }
        /* get_final_stacks failed: fall back to the legacy evaluation. */
    }
    for (int p = 0; p < num_players; ++p)
        out[p] = game->get_utility(game, state_key, p, user_data);
}

/* Single-player variant used by best-response / exploitability traversals:
 * evaluates exactly one player's utility instead of the whole vector. */
static double cfr_terminal_utility(cfr_game_t *game, uint64_t state_key,
                                   int player, int num_players, void *user_data)
{
    if (num_players > CFR_MAX_PLAYERS)
        num_players = CFR_MAX_PLAYERS;
    if (game && game->utility.utility_fn && game->get_final_stacks &&
        player >= 0 && player < num_players)
    {
        int32_t stacks[CFR_MAX_PLAYERS];
        if (game->get_final_stacks(game, state_key, stacks, user_data) == 0)
            return game->utility.utility_fn(stacks, num_players, player,
                                            game->utility.user_data);
        /* get_final_stacks failed: fall back to the legacy evaluation. */
    }
    return game->get_utility(game, state_key, player, user_data);
}

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

/* Exploitability metric used by cfr_solve. Multiway games (num_players > 2)
 * use the full N-player best-response metric; 2-player (or unspecified) games
 * use the exact 2-player best-response. Both receive game->game_data as the
 * traversal user_data, consistent with the rest of the solve.
 *
 * The computation walks the game tree twice, so cfr_solve gates it on
 * cfr_config_t::exploitability_interval and caches the result per iteration. */
typedef struct
{
    double value;
    int iteration;
    int valid;
} cfr_exploitability_cache_t;

static double cfr_solve_exploitability(cfr_game_t *game, cfr_storage_t *storage)
{
    void *user_data = game->game_data;
    if (game->num_players > 2)
    {
        cfr_exploitability_result_t result;
        if (cfr_exploitability_multiway(game, storage, user_data, &result) != 0)
            return 0.0;
        return result.total_exploitability;
    }
    return cfr_exploitability_perfect_info(game, storage, user_data);
}

static double cfr_solve_exploitability_cached(cfr_game_t *game,
                                              cfr_storage_t *storage,
                                              cfr_exploitability_cache_t *cache,
                                              int iteration)
{
    if (cache->valid && cache->iteration == iteration)
        return cache->value;
    cache->value = cfr_solve_exploitability(game, storage);
    cache->iteration = iteration;
    cache->valid = 1;
    return cache->value;
}

double cfr_solve(
    cfr_game_t *game,
    cfr_storage_t *storage,
    const cfr_config_t *config,
    double *out_exploitability)
{
    cfr_walk_ctx_t walk;
    cfr_algo_ops_t algo;
    if (!game || !storage || !config)
    {
        if (out_exploitability)
            *out_exploitability = -1.0;
        return -1.0;
    }

    cfr_walk_ctx_init(&walk);
    algo = cfr_algo_ops_from_config(config);
    /* Resolved once. NULL keeps the historical stderr behaviour, so an
       existing caller sees the same bytes it always did. */
    if (config->telemetry != NULL)
        walk.telemetry = config->telemetry;

    cfr_storage_set_strategy_mode_for(storage, config->enable_ecfr, config->ecfr_lambda);
    cfr_storage_set_memory_masks(storage, config->keep_avg_strategy_mask, config->keep_ev_mask);
    walk.use_flow_focus = config->enable_mccfvfp ? 1 : 0;
    walk.flow_pow = (fabs(config->mccfvfp_flow_pow) > 1e-9) ? config->mccfvfp_flow_pow : 1.0;

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
                pe_telemetry_emitf(walk.telemetry, PE_LOG_INFO, "cfr", (uint64_t)start_iter,
                               "[cfr] resumed from %s at iteration %d\n", config->resume_path, start_iter);
            }
        }
        else
        {
            pe_telemetry_emitf(walk.telemetry, PE_LOG_WARN, "cfr", 0,
                               "[cfr] warning: failed to resume from %s (%s)\n", config->resume_path, strerror(errno));
        }
    }

    int last_iter = start_iter;
    int aborted = 0;
    cfr_exploitability_cache_t expl_cache = {0.0, -1, 0};
    long long metrics_nodes_total = 0;
    double metrics_elapsed_total = 0.0;
    int metrics_interval_raw = config->metrics_interval;
    if (metrics_interval_raw <= 0)
        metrics_interval_raw = 1;

    int depth_limit = config->max_depth > 0 ? config->max_depth : CFR_DEFAULT_MAX_DEPTH;
    /* One scratch buffer for the whole solve: 3 arrays (strategy,
       regret_delta, action_util) of CFR_MAX_ACTIONS doubles per depth
       level.  Replaces the old per-frame alloca. */
    /* Depth is numbered from 1 and the guard permits depth == depth_limit,
       so reserve frames 0 through depth_limit inclusive. */
    double *scratch = (double *)calloc(((size_t)depth_limit + 1u) * 3u *
                                           (size_t)CFR_MAX_ACTIONS,
                                       sizeof(double));
    if (!scratch)
    {
        pe_telemetry_emitf(walk.telemetry, PE_LOG_ERROR, "cfr", 0,
                           "[cfr] error: failed to allocate traversal scratch buffer\n");
        return -1.0;
    }

    /* num_players is fixed for the whole solve; hoist it (and the per-player
     * reach/util scratch) OUT of the iteration loop. Allocating reach/util with
     * alloca inside the loop grew the stack frame by ~2*num_players doubles on
     * EVERY iteration (alloca is only reclaimed at function return), overflowing
     * the stack on high-iteration solves (segfault on Windows/Linux at ~10^5
     * iterations). Fixed-size arrays reused each iteration are correct because
     * they are fully re-initialised at the top of every pass. */
    int num_players = (game->num_players > 0) ? game->num_players : 2;
    if (num_players > CFR_MAX_PLAYERS)
    {
        pe_telemetry_emitf(walk.telemetry, PE_LOG_ERROR, "cfr", 0,
                           "[cfr] error: num_players=%d exceeds max supported (%d)\n",
                           num_players, CFR_MAX_PLAYERS);
        free(scratch);
        return -1.0;
    }

    /* ISSUE-14 (#170): apply a default terminal utility config from the config
     * when the game has not been configured explicitly, so the main walk, the
     * best-response/exploitability pass and policy-value computations all see it. */
    if (!game->utility.utility_fn && config->utility.utility_fn)
        game->utility = config->utility;

    double reach[CFR_MAX_PLAYERS];
    double util[CFR_MAX_PLAYERS];

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
            pe_telemetry_emitf(walk.telemetry, PE_LOG_TRACE, "cfr", (uint64_t)(it + 1),
                               "[cfr] iter %d/%d started\n", it + 1, config->max_iterations);
        }
        walk.current_iter = it + 1;
        walk.recursion_depth = 0;
        walk.max_depth = 0;
        walk.node_count = 0;

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
            /* Start a fresh reach-weighted EV-loss accumulation when this is a
               periodic relock iteration, so the recorded loss reflects only the
               current pass over the infoset's states. */
            if (config->enable_periodic_relock && config->lock_period > 0 &&
                (((it + 1) % config->lock_period) == 0))
                cfr_storage_begin_lock_ev_pass(storage);
            walk.depth_exceeded = 0;
            /* EXT-07: the discount applies to what was accumulated before this
               iteration, exactly once — R_t = R_(t-1) * d(t) + r_t. Doing it
               here rather than inside the recursion is the whole correction. */
            cfr_storage_scale_regrets(storage, algo.regret_discount(&algo, it));
            cfr_traverse_recursive(game, storage, config, &algo, root_key, reach, num_players, it, util, NULL,
                                   scratch, depth_limit, &walk);
        }

        if (walk.depth_exceeded)
        {
            pe_telemetry_emitf(walk.telemetry, PE_LOG_ERROR, "cfr", (uint64_t)(it + 1),
                               "[cfr] error: aborting solve after recursion depth limit %d exceeded\n",
                               depth_limit);
            free(scratch);
            return -1.0;
        }

        if (config->stop_flag && *config->stop_flag)
        {
            aborted = 1;
            break;
        }

        double iter_elapsed = (double)(clock() - iter_start_clock) / CLOCKS_PER_SEC;
        metrics_elapsed_total += iter_elapsed;
        metrics_nodes_total += walk.node_count;

        if (config->trace_iterations)
        {
            pe_telemetry_emitf(walk.telemetry, PE_LOG_TRACE, "cfr", (uint64_t)(it + 1),
                               "[cfr] iter %d/%d finished in %.3fs (nodes=%ld, max_depth=%d)\n",
                               it + 1, config->max_iterations, iter_elapsed, walk.node_count, walk.max_depth);
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
                snap.nodes_iteration = walk.node_count;
                snap.nodes_total = metrics_nodes_total;
                snap.nodes_per_sec = (iter_elapsed > 0.0) ? ((double)walk.node_count / iter_elapsed) : 0.0;
                snap.iterations_per_sec = (metrics_elapsed_total > 0.0) ? ((double)(it + 1) / metrics_elapsed_total) : 0.0;
                snap.infosets_total = cfr_storage_count_infosets(storage);
                snap.num_players = (game->num_players > 0) ? game->num_players : 0;

                if (config->metrics_level >= 1 && config->exploitability_interval > 0 &&
                    ((it + 1) % config->exploitability_interval) == 0)
                    snap.exploitability = cfr_solve_exploitability_cached(game, storage, &expl_cache, (int)(it + 1));

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
            pe_telemetry_emitf(walk.telemetry, PE_LOG_INFO, "cfr", (uint64_t)(it + 1),
                               "[cfr] iter %d/%d  elapsed %.2fs  est %.2fs  eta %.2fs\n",
                               it + 1, config->max_iterations, elapsed, est_total, eta);
            if (config->monitor_fn && config->monitor_period == 0)
                config->monitor_fn(it + 1, game, storage, config->monitor_user);
        }

        if (config->checkpoint_path && config->checkpoint_interval > 0 && (it + 1) % config->checkpoint_interval == 0)
        {
            if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)(it + 1)) != 0)
            {
                pe_telemetry_emitf(walk.telemetry, PE_LOG_WARN, "cfr", (uint64_t)(it + 1),
                                   "[cfr] warning: failed to write checkpoint %s (%s)\n",
                                   config->checkpoint_path, strerror(errno));
            }
        }

        if (config->exploitability_interval > 0 && ((it + 1) % config->exploitability_interval) == 0)
        {
            double exploitability = cfr_solve_exploitability_cached(game, storage, &expl_cache, (int)(it + 1));
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

    double final_exploitability = cfr_solve_exploitability_cached(game, storage, &expl_cache, (int)(last_iter + 1));
    if (out_exploitability)
    {
        *out_exploitability = final_exploitability;
    }

    if (progress_interval > 0 && config->max_iterations > 0)
    {
        double total_elapsed = (double)(clock() - start_clock) / CLOCKS_PER_SEC;
        pe_telemetry_emitf(walk.telemetry, PE_LOG_INFO, "cfr", (uint64_t)config->max_iterations,
                           "[cfr] iter %d/%d  elapsed %.2fs  est %.2fs  eta 0.00s\n",
                           config->max_iterations, config->max_iterations, total_elapsed, total_elapsed);
    }

    /* Restore the default so a storage reused for a second solve does not
       inherit this one's ECFR temperature. */
    cfr_storage_set_strategy_mode_for(storage, 0, 1.0);
    walk.use_flow_focus = 0;
    walk.flow_pow = 1.0;

    if (config->checkpoint_path && config->checkpoint_final)
    {
        if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)config->max_iterations) != 0)
        {
            pe_telemetry_emitf(walk.telemetry, PE_LOG_WARN, "cfr", 0,
                               "[cfr] warning: failed to write final checkpoint %s (%s)\n",
                               config->checkpoint_path, strerror(errno));
        }
    }
    else if (config->checkpoint_path && config->stop_flag && *config->stop_flag)
    {
        if (cfr_storage_save_checkpoint(storage, config->checkpoint_path, (uint64_t)last_iter) != 0)
        {
            pe_telemetry_emitf(walk.telemetry, PE_LOG_WARN, "cfr", 0,
                               "[cfr] warning: failed to write checkpoint %s (%s)\n",
                               config->checkpoint_path, strerror(errno));
        }
    }

    if (config->monitor_fn && config->monitor_period == 0)
        config->monitor_fn(last_iter, game, storage, config->monitor_user);

    if (aborted)
        pe_telemetry_emitf(walk.telemetry, PE_LOG_INFO, "cfr", (uint64_t)last_iter,
                           "[cfr] stopped at iteration %d\n", last_iter);

    free(scratch);
    return final_exploitability;
}


double cfr_best_response_recursive(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    int current_player,
    uint64_t state_key,
    void *user_data,
    int depth,
    int *depth_exceeded,
    const pe_telemetry_ops_t *telemetry)
{
    if (depth > CFR_DEFAULT_MAX_DEPTH)
    {
        if (!*depth_exceeded)
        {
            pe_telemetry_emitf(telemetry, PE_LOG_ERROR, "cfr", 0,
                               "[cfr] error: best-response recursion depth exceeded %d at 0x%llx\n",
                               CFR_DEFAULT_MAX_DEPTH, (unsigned long long)state_key);
        }
        *depth_exceeded = 1;
        return 0.0;
    }
    if (game->is_terminal(game, state_key, user_data))
    {
        int np = game->num_players > 0 ? game->num_players : 2;
        return cfr_terminal_utility(game, state_key, br_player, np, user_data);
    }

    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0 || !game->apply_chance)
            return 0.0;
        double chance_value = 0.0;
        double chance_weight_sum = 0.0;
        for (int i = 0; i < outcomes; ++i)
        {
            double w = cfr_chance_weight(game, state_key, i, user_data);
            chance_weight_sum += w;
            uint64_t child_key = game->apply_chance(game, state_key, i, user_data);
            int child_player = game->current_player
                ? game->current_player(game, child_key, user_data)
                : 1 - current_player;
            chance_value += w * cfr_best_response_recursive(
                game, storage, br_player, child_player, child_key,
                user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, child_key, user_data);
        }
        return chance_value / ((chance_weight_sum > 0.0)
                                   ? chance_weight_sum
                                   : (double)outcomes);
    }

    int actions[CFR_MAX_ACTIONS];
    int num_actions = game->get_actions(game, state_key, actions, CFR_MAX_ACTIONS, user_data);
    if (num_actions <= 0)
        return 0.0;
    if (num_actions > CFR_MAX_ACTIONS)
        num_actions = CFR_MAX_ACTIONS;

    if (current_player == br_player)
    {
        /* Terminal utilities are already expressed from br_player's point of
           view, so every player maximizes their own returned utility. */
        double best_value = -1e100;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            double value = cfr_best_response_recursive(game, storage, br_player, 1 - current_player, next_state_key, user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, next_state_key, user_data);
            if (value > best_value)
                best_value = value;
        }
        return best_value;
    }
    else
    {
        double *avg_strategy = (double *)alloca(sizeof(double) * num_actions);
        cfr_storage_get_avg_strategy(storage, cfr_traversal_storage_key(game, state_key), num_actions, avg_strategy);
        double node_value = 0.0;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            node_value += avg_strategy[i] * cfr_best_response_recursive(game, storage, br_player, 1 - current_player, next_state_key, user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, next_state_key, user_data);
        }
        return node_value;
    }
}

double cfr_best_response_perfect_info(
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
    int root_player = game->current_player
        ? game->current_player(game, root_key, user_data)
        : 0;
    {
        int depth_exceeded = 0;
        return cfr_best_response_recursive(game, storage, player, root_player,
                                       root_key, user_data, 0, &depth_exceeded,
                                       pe_telemetry_stderr());
    }
}

double cfr_best_response_value(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data)
{
    return cfr_best_response_perfect_info(game, storage, player, user_data);
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

double cfr_exploitability_perfect_info(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data)
{
    if (game->num_players && game->num_players != 2)
        return 0.0;
    double br_p0 = cfr_best_response_perfect_info(game, storage, 0, user_data);
    double br_p1 = cfr_best_response_perfect_info(game, storage, 1, user_data);
    // Exploitability is the sum of best response values for a zero-sum game
    return br_p0 + br_p1;
}

double cfr_exploitability(
    cfr_game_t *game,
    cfr_storage_t *storage,
    void *user_data)
{
    return cfr_exploitability_perfect_info(game, storage, user_data);
}

/* ===== Multiway Best-Response and Exploitability ===== */

/**
 * Internal recursive best-response for N-player games.
 * The BR player maximizes their value; all others follow avg strategy from storage.
 */
double cfr_best_response_recursive_multiway(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int br_player,
    uint64_t state_key,
    void *user_data,
    int depth,
    int *depth_exceeded,
    const pe_telemetry_ops_t *telemetry)
{
    /* Terminal state - return utility for BR player */
    if (game->is_terminal(game, state_key, user_data))
    {
        int np = game->num_players > 0 ? game->num_players : 2;
        return cfr_terminal_utility(game, state_key, br_player, np, user_data);
    }

    if (depth > CFR_DEFAULT_MAX_DEPTH)
    {
        if (!*depth_exceeded)
        {
            pe_telemetry_emitf(telemetry, PE_LOG_ERROR, "cfr", 0,
                               "[cfr] error: best-response recursion depth exceeded %d at 0x%llx\n",
                               CFR_DEFAULT_MAX_DEPTH, (unsigned long long)state_key);
        }
        *depth_exceeded = 1;
        return 0.0;
    }

    if (game->is_chance && game->is_chance(game, state_key, user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, user_data)
            : 0;
        if (outcomes <= 0 || !game->apply_chance)
            return 0.0;
        double chance_value = 0.0;
        double chance_weight_sum = 0.0;
        for (int i = 0; i < outcomes; ++i)
        {
            double w = cfr_chance_weight(game, state_key, i, user_data);
            chance_weight_sum += w;
            uint64_t child_key = game->apply_chance(game, state_key, i, user_data);
            chance_value += w * cfr_best_response_recursive_multiway(
                game, storage, br_player, child_key, user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, child_key, user_data);
        }
        return chance_value / ((chance_weight_sum > 0.0)
                                   ? chance_weight_sum
                                   : (double)outcomes);
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
    if (num_actions <= 0)
        return 0.0;
    if (num_actions > 32)
        num_actions = 32;

    if (current_player == br_player)
    {
        /* BR player: maximize expected value */
        double best_value = -1e100;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            double value = cfr_best_response_recursive_multiway(game, storage, br_player, next_state_key, user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, next_state_key, user_data);
            if (value > best_value)
                best_value = value;
        }
        return best_value;
    }
    else
    {
        /* Other player: follow average strategy */
        double *avg_strategy = (double *)alloca(sizeof(double) * num_actions);
        cfr_storage_get_avg_strategy(storage, cfr_traversal_storage_key(game, state_key), num_actions, avg_strategy);
        
        double node_value = 0.0;
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t next_state_key = game->apply_action(game, state_key, actions[i], user_data);
            node_value += avg_strategy[i] * cfr_best_response_recursive_multiway(game, storage, br_player, next_state_key, user_data, depth + 1, depth_exceeded, telemetry);
            if (game->release_state)
                game->release_state(game, next_state_key, user_data);
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
            return cfr_best_response_perfect_info(game, storage, player, user_data);
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
    
    {
        int depth_exceeded = 0;
        return cfr_best_response_recursive_multiway(game, storage, player, root_key,
                                                user_data, 0, &depth_exceeded,
                                                pe_telemetry_stderr());
    }
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

static int cfr_audit_metric_finite(double value)
{
    return value >= -DBL_MAX && value <= DBL_MAX;
}

typedef struct
{
    uint64_t key;
    int num_actions;
    int actions[CFR_MAX_ACTIONS];
    int selected;
    double action_values[CFR_MAX_ACTIONS];
} cfr_audit_infoset_t;

typedef struct
{
    cfr_game_t *game;
    cfr_storage_t *storage;
    int br_player;
    void *user_data;
    cfr_audit_infoset_t *infosets;
    size_t count;
    size_t capacity;
    int failed;
} cfr_audit_br_context_t;

static cfr_audit_infoset_t *cfr_audit_find_infoset(
    cfr_audit_br_context_t *ctx, uint64_t key, int num_actions,
    const int *actions)
{
    for (size_t i = 0; i < ctx->count; ++i)
        if (ctx->infosets[i].key == key)
        {
            if (ctx->infosets[i].num_actions != num_actions)
                ctx->failed = 1;
            return &ctx->infosets[i];
        }

    if (ctx->count == ctx->capacity)
    {
        size_t capacity = ctx->capacity ? ctx->capacity * 2 : 64;
        cfr_audit_infoset_t *grown = (cfr_audit_infoset_t *)realloc(
            ctx->infosets, capacity * sizeof(*grown));
        if (!grown)
        {
            ctx->failed = 1;
            return NULL;
        }
        ctx->infosets = grown;
        ctx->capacity = capacity;
    }

    cfr_audit_infoset_t *entry = &ctx->infosets[ctx->count++];
    memset(entry, 0, sizeof(*entry));
    entry->key = key;
    entry->num_actions = num_actions;
    entry->selected = 0;
    for (int i = 0; i < num_actions; ++i)
        entry->actions[i] = actions[i];
    return entry;
}

static double cfr_audit_br_value(cfr_audit_br_context_t *ctx,
                                 uint64_t state_key,
                                 int depth);

static void cfr_audit_collect(cfr_audit_br_context_t *ctx,
                              uint64_t state_key,
                              double counterfactual_reach,
                              int depth)
{
    cfr_game_t *game = ctx->game;
    if (ctx->failed || depth > CFR_DEFAULT_MAX_DEPTH ||
        game->is_terminal(game, state_key, ctx->user_data))
        return;

    if (game->is_chance && game->is_chance(game, state_key, ctx->user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, ctx->user_data) : 0;
        double total_weight = 0.0;
        for (int i = 0; i < outcomes; ++i)
            total_weight += cfr_chance_weight(game, state_key, i, ctx->user_data);
        if (outcomes <= 0 || total_weight <= 0.0)
            return;
        for (int i = 0; i < outcomes; ++i)
        {
            uint64_t child = game->apply_chance(game, state_key, i,
                                                ctx->user_data);
            double weight = cfr_chance_weight(game, state_key, i,
                                              ctx->user_data) / total_weight;
            cfr_audit_collect(ctx, child, counterfactual_reach * weight,
                              depth + 1);
            if (game->release_state)
                game->release_state(game, child, ctx->user_data);
        }
        return;
    }

    int current_player = game->current_player
        ? game->current_player(game, state_key, ctx->user_data) : -1;
    int actions[CFR_MAX_ACTIONS];
    int num_actions = game->get_actions(game, state_key, actions,
                                        CFR_MAX_ACTIONS, ctx->user_data);
    if (current_player < 0 || num_actions <= 0 || num_actions > CFR_MAX_ACTIONS)
        return;

    if (current_player == ctx->br_player)
    {
        cfr_audit_infoset_t *entry = cfr_audit_find_infoset(
            ctx, cfr_traversal_storage_key(game, state_key), num_actions, actions);
        if (!entry)
            return;
        size_t entry_index = (size_t)(entry - ctx->infosets);
        for (int i = 0; i < num_actions; ++i)
        {
            uint64_t child = game->apply_action(game, state_key, actions[i],
                                                ctx->user_data);
            double child_value = cfr_audit_br_value(ctx, child, depth + 1);
            /* Recursive discovery can realloc the infoset table. Reacquire
             * the entry by index before accumulating its action value. */
            entry = &ctx->infosets[entry_index];
            entry->action_values[i] += counterfactual_reach * child_value;
            /* Explore every own-action branch when collecting infosets: a
             * future information set must not disappear just because the
             * current provisional BR policy does not select this action. */
            cfr_audit_collect(ctx, child, counterfactual_reach, depth + 1);
            if (game->release_state)
                game->release_state(game, child, ctx->user_data);
        }
        return;
    }

    double avg_strategy[CFR_MAX_ACTIONS];
    cfr_storage_get_avg_strategy(ctx->storage,
                                 cfr_traversal_storage_key(game, state_key),
                                 num_actions, avg_strategy);
    for (int i = 0; i < num_actions; ++i)
    {
        uint64_t child = game->apply_action(game, state_key, actions[i],
                                            ctx->user_data);
        cfr_audit_collect(ctx, child, counterfactual_reach * avg_strategy[i],
                          depth + 1);
        if (game->release_state)
            game->release_state(game, child, ctx->user_data);
    }
}

static double cfr_audit_br_value(cfr_audit_br_context_t *ctx,
                                 uint64_t state_key,
                                 int depth)
{
    cfr_game_t *game = ctx->game;
    if (ctx->failed || depth > CFR_DEFAULT_MAX_DEPTH)
        return 0.0;
    if (game->is_terminal(game, state_key, ctx->user_data))
        return cfr_terminal_utility(game, state_key, ctx->br_player,
                                    game->num_players > 0 ? game->num_players : 2,
                                    ctx->user_data);

    if (game->is_chance && game->is_chance(game, state_key, ctx->user_data))
    {
        int outcomes = game->get_chance_outcomes
            ? game->get_chance_outcomes(game, state_key, ctx->user_data) : 0;
        double total_weight = 0.0;
        double value = 0.0;
        for (int i = 0; i < outcomes; ++i)
            total_weight += cfr_chance_weight(game, state_key, i,
                                              ctx->user_data);
        if (outcomes <= 0 || total_weight <= 0.0)
            return 0.0;
        for (int i = 0; i < outcomes; ++i)
        {
            uint64_t child = game->apply_chance(game, state_key, i,
                                                ctx->user_data);
            value += cfr_chance_weight(game, state_key, i,
                                       ctx->user_data) / total_weight *
                     cfr_audit_br_value(ctx, child, depth + 1);
            if (game->release_state)
                game->release_state(game, child, ctx->user_data);
        }
        return value;
    }

    int current_player = game->current_player
        ? game->current_player(game, state_key, ctx->user_data) : -1;
    int actions[CFR_MAX_ACTIONS];
    int num_actions = game->get_actions(game, state_key, actions,
                                        CFR_MAX_ACTIONS, ctx->user_data);
    if (current_player < 0 || num_actions <= 0 || num_actions > CFR_MAX_ACTIONS)
        return 0.0;

    if (current_player == ctx->br_player)
    {
        cfr_audit_infoset_t *entry = cfr_audit_find_infoset(
            ctx, cfr_traversal_storage_key(game, state_key), num_actions, actions);
        int selected = entry ? entry->selected : 0;
        if (selected < 0 || selected >= num_actions)
            selected = 0;
        uint64_t child = game->apply_action(game, state_key, actions[selected],
                                            ctx->user_data);
        double value = cfr_audit_br_value(ctx, child, depth + 1);
        if (game->release_state)
            game->release_state(game, child, ctx->user_data);
        return value;
    }

    double avg_strategy[CFR_MAX_ACTIONS];
    cfr_storage_get_avg_strategy(ctx->storage,
                                 cfr_traversal_storage_key(game, state_key),
                                 num_actions, avg_strategy);
    double value = 0.0;
    for (int i = 0; i < num_actions; ++i)
    {
        uint64_t child = game->apply_action(game, state_key, actions[i],
                                            ctx->user_data);
        value += avg_strategy[i] * cfr_audit_br_value(ctx, child, depth + 1);
        if (game->release_state)
            game->release_state(game, child, ctx->user_data);
    }
    return value;
}

static double cfr_best_response_value_infoset_run(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data,
    int *out_iterations,
    int *out_converged)
{
    cfr_audit_br_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.game = game;
    ctx.storage = storage;
    ctx.br_player = player;
    ctx.user_data = user_data;

    uint64_t root_key = (uint64_t)game->initial_state;
    if (!root_key && game->initial_state)
        root_key = (uint64_t)(uintptr_t)game->initial_state;

    int iterations = 0;
    int converged = 0;
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        iterations = iteration + 1;
        for (size_t i = 0; i < ctx.count; ++i)
            memset(ctx.infosets[i].action_values, 0,
                   sizeof(ctx.infosets[i].action_values));
        size_t before = ctx.count;
        cfr_audit_collect(&ctx, root_key, 1.0, 0);
        if (ctx.failed)
        {
            free(ctx.infosets);
            if (out_iterations)
                *out_iterations = iterations;
            if (out_converged)
                *out_converged = 0;
            return 0.0;
        }

        int changed = (ctx.count != before);
        for (size_t i = 0; i < ctx.count; ++i)
        {
            int best = 0;
            for (int action = 1; action < ctx.infosets[i].num_actions; ++action)
                if (ctx.infosets[i].action_values[action] >
                    ctx.infosets[i].action_values[best])
                    best = action;
            if (best != ctx.infosets[i].selected)
            {
                ctx.infosets[i].selected = best;
                changed = 1;
            }
        }
        if (!changed && iteration > 0)
        {
            converged = 1;
            break;
        }
    }

    double result = cfr_audit_br_value(&ctx, root_key, 0);
    free(ctx.infosets);
    if (out_iterations)
        *out_iterations = iterations;
    if (out_converged)
        *out_converged = converged;
    return result;
}

double cfr_best_response_value_infoset(cfr_game_t *game,
                                       cfr_storage_t *storage,
                                       int player,
                                       void *user_data)
{
    return cfr_best_response_value_infoset_run(game, storage, player,
                                               user_data, NULL, NULL);
}

int cfr_best_response_value_infoset_ex(
    cfr_game_t *game,
    cfr_storage_t *storage,
    int player,
    void *user_data,
    cfr_best_response_infoset_result_t *out_result)
{
    if (!game || !storage || !out_result)
        return -1;
    memset(out_result, 0, sizeof(*out_result));
    out_result->value = cfr_best_response_value_infoset_run(
        game, storage, player, user_data, &out_result->iterations,
        &out_result->converged);
    return 0;
}

int cfr_audit_multiway(cfr_game_t *game,
                       cfr_storage_t *storage,
                       void *user_data,
                       cfr_multiway_audit_result_t *out_result)
{
    if (!game || !storage || !out_result)
        return -1;

    cfr_policy_value_result_t policy;
    int num_players = game->num_players > 0 ? game->num_players : 2;
    if (num_players <= 0 || num_players > CFR_MAX_PLAYERS ||
        cfr_compute_policy_values_detailed(game, storage, user_data, &policy) != 0)
        return -1;

    memset(out_result, 0, sizeof(*out_result));
    out_result->num_players = num_players;
    double policy_sum = 0.0;
    double cce_gap = 0.0;
    for (int p = 0; p < num_players; ++p)
    {
        const double br_value = cfr_best_response_value_infoset(
            game, storage, p, user_data);
        const double player_exploitability = br_value - policy.ev[p];
        out_result->max_player_exploitability[p] = player_exploitability;
        if (!cfr_audit_metric_finite(br_value) ||
            !cfr_audit_metric_finite(policy.ev[p]) ||
            !cfr_audit_metric_finite(player_exploitability))
            out_result->has_nonfinite_metrics = 1;
        if (player_exploitability > cce_gap)
            cce_gap = player_exploitability;
        policy_sum += policy.ev[p];
    }

    out_result->cce_gap = cce_gap > 0.0 ? cce_gap : 0.0;
    out_result->total_pot_ev_imbalance = fabs(policy_sum);
    if (!cfr_audit_metric_finite(out_result->total_pot_ev_imbalance) ||
        !cfr_audit_metric_finite(out_result->cce_gap))
        out_result->has_nonfinite_metrics = 1;
    out_result->has_collusive_ev_transfer =
        game->utility.utility_fn == NULL &&
        out_result->total_pot_ev_imbalance > 1e-9;
    return out_result->has_nonfinite_metrics ? -1 : 0;
}

void cfr_exploitability_print(const cfr_exploitability_result_t *result)
{
    if (!result)
    {
        pe_telemetry_emitf(pe_telemetry_stdout(), PE_LOG_INFO, "exploitability", 0,
                           "Exploitability result: NULL\n");
        return;
    }
    
    pe_telemetry_emitf(pe_telemetry_stdout(), PE_LOG_INFO, "exploitability", 0,
                       "=== Exploitability Analysis (%d players) ===\n", result->num_players);
    for (int p = 0; p < result->num_players; ++p)
    {
        pe_telemetry_emitf(pe_telemetry_stdout(), PE_LOG_INFO, "exploitability", 0,
                           "Player %d: policy_EV=%.6f  BR_EV=%.6f  exploit=%.6f\n",
                           p, result->policy_value[p], result->br_value[p], result->exploitability[p]);
    }
    pe_telemetry_emitf(pe_telemetry_stdout(), PE_LOG_INFO, "exploitability", 0,
                       "Total exploitability: %.6f\n", result->total_exploitability);
    pe_telemetry_emitf(pe_telemetry_stdout(), PE_LOG_INFO, "exploitability", 0,
                       "Nash distance estimate: %.6f\n", result->nash_distance);
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
    double *out_util,
    int depth,
    int *depth_exceeded,
    const pe_telemetry_ops_t *telemetry)
{
    ctx->nodes_visited++;

    /* Initialize output */
    for (int p = 0; p < ctx->num_players; ++p)
        out_util[p] = 0.0;

    if (depth > CFR_DEFAULT_MAX_DEPTH)
    {
        if (!*depth_exceeded)
        {
            pe_telemetry_emitf(telemetry, PE_LOG_ERROR, "cfr", 0,
                               "[cfr] error: policy-value recursion depth exceeded %d at 0x%llx\n",
                               CFR_DEFAULT_MAX_DEPTH, (unsigned long long)state_key);
        }
        *depth_exceeded = 1;
        return;
    }

    /* Check terminal state */
    if (ctx->game->is_terminal(ctx->game, state_key, ctx->user_data))
    {
        /* Compute reach probability product */
        double reach_prod = 1.0;
        for (int p = 0; p < ctx->num_players; ++p)
            reach_prod *= reach[p];

        /* Accumulate terminal utilities, routed through the generic utility
         * abstraction (ISSUE-14, #170) when configured. */
        double term_util[CFR_MAX_PLAYERS];
        cfr_traversal_terminal_utilities(ctx->game, state_key, ctx->num_players, term_util,
                               ctx->user_data);
        for (int p = 0; p < ctx->num_players; ++p)
        {
            out_util[p] = term_util[p];

            /* Weighted accumulation for expected value */
            ctx->ev_sum[p] += reach_prod * term_util[p];
            ctx->ev_sq_sum[p] += reach_prod * term_util[p] * term_util[p];
        }
        ctx->reach_sum += reach_prod;
        return;
    }

    if (ctx->game->is_chance &&
        ctx->game->is_chance(ctx->game, state_key, ctx->user_data))
    {
        int outcomes = ctx->game->get_chance_outcomes
            ? ctx->game->get_chance_outcomes(ctx->game, state_key, ctx->user_data)
            : 0;
        if (outcomes <= 0 || !ctx->game->apply_chance)
            return;
        double child_util[CFR_MAX_PLAYERS];
        double chance_weight_sum = 0.0;
        for (int p = 0; p < ctx->num_players; ++p)
            out_util[p] = 0.0;
        for (int c = 0; c < outcomes; ++c)
        {
            double w = cfr_chance_weight(ctx->game, state_key, c, ctx->user_data);
            chance_weight_sum += w;
            uint64_t child_key = ctx->game->apply_chance(
                ctx->game, state_key, c, ctx->user_data);
            policy_value_recursive(ctx, child_key, reach, child_util, depth + 1, depth_exceeded, telemetry);
            for (int p = 0; p < ctx->num_players; ++p)
                out_util[p] += w * child_util[p];
            if (ctx->game->release_state)
                ctx->game->release_state(ctx->game, child_key, ctx->user_data);
        }
        double chance_norm = (chance_weight_sum > 0.0)
                                 ? chance_weight_sum
                                 : (double)outcomes;
        for (int p = 0; p < ctx->num_players; ++p)
            out_util[p] /= chance_norm;
        return;
    }

    /* Get available actions */
    int actions[CFR_MAX_ACTIONS];
    int num_actions = ctx->game->get_actions(ctx->game, state_key, actions, CFR_MAX_ACTIONS, ctx->user_data);
    if (num_actions <= 0)
        return;
    if (num_actions > CFR_MAX_ACTIONS)
        num_actions = CFR_MAX_ACTIONS;

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
    cfr_storage_get_avg_strategy(ctx->storage, cfr_traversal_storage_key(ctx->game, state_key), num_actions, avg_strategy);

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
        policy_value_recursive(ctx, next_state, next_reach, child_util, depth + 1, depth_exceeded, telemetry);
        if (ctx->game->release_state)
            ctx->game->release_state(ctx->game, next_state, ctx->user_data);

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
    {
        int depth_exceeded = 0;
        policy_value_recursive(&ctx, root_key, reach, util, 0, &depth_exceeded,
                               pe_telemetry_stderr());
    }

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
    {
        int depth_exceeded = 0;
        policy_value_recursive(&ctx, root_key, reach, util, 0, &depth_exceeded,
                               pe_telemetry_stderr());
    }

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
