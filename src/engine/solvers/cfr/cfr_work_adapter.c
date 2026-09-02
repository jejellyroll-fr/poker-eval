/* cfr_work_adapter.c - concrete distributed CFR execution */

#include <poker_eval/engine/solvers/cfr/cfr_work_adapter.h>
#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <poker_eval/core/modern_cardmask.h>

#include "../../../solver/domain/finite_double.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int pe_cfr_work_execute(const pe_work_unit_t *unit,
                        pe_compute_kind_t backend,
                        pe_work_result_t *out_result,
                        void *user_data)
{
    const pe_cfr_work_executor_config_t *config =
        (const pe_cfr_work_executor_config_t *)user_data;
    cfr_game_t game;
    cfr_storage_t *storage = NULL;
    uint8_t *delta = NULL;
    size_t delta_size = 0u;
    int iterations;
    double exploitability = 0.0;
    clock_t started;
    clock_t elapsed;
    int rc;

    if (!unit || !out_result || !config || !config->build_game ||
        backend <= PE_COMPUTE_AUTO || backend >= PE_COMPUTE_COUNT ||
        pe_work_unit_validate(unit) != 0 ||
        unit->iteration_end - unit->iteration_begin > (uint64_t)INT_MAX)
        return -1;
    out_result->public_state = unit->public_state;
    out_result->iteration_begin = unit->iteration_begin;
    out_result->iteration_end = unit->iteration_end;
    out_result->backend = backend;
    out_result->constraints_satisfied = 1;
    out_result->delta = NULL;
    out_result->delta_size = 0u;
    out_result->delta_owned = 0;
    iterations = (int)(unit->iteration_end - unit->iteration_begin);
    if (iterations <= 0)
        return -1;
    memset(&game, 0, sizeof(game));
    rc = config->build_game(unit, backend, &game, config->user_data);
    if (rc != 0)
        return -1;
    storage = cfr_storage_create();
    if (!storage) {
        if (config->destroy_game)
            config->destroy_game(&game, config->user_data);
        return -1;
    }
    if (unit->regret_count != 0u) {
        int actions[CFR_MAX_ACTIONS];
        int action_count;
        uint64_t infoset = game.get_infoset_key
            ? game.get_infoset_key(game.initial_state) : unit->public_state;
        if (!game.get_actions || unit->regret_count > CFR_MAX_ACTIONS) {
            cfr_storage_destroy(storage);
            if (config->destroy_game)
                config->destroy_game(&game, config->user_data);
            return -1;
        }
        action_count = game.get_actions(&game,
                                        (uint64_t)(uintptr_t)game.initial_state,
                                        actions, CFR_MAX_ACTIONS,
                                        game.game_data);
        if (action_count <= 0 || (size_t)action_count != unit->regret_count) {
            cfr_storage_destroy(storage);
            if (config->destroy_game)
                config->destroy_game(&game, config->user_data);
            return -1;
        }
        cfr_storage_update_regret(storage, infoset, action_count,
                                  unit->regret_snapshot, 1.0);
    }
    {
        cfr_config_t solve_config = config->cfr;
        solve_config.max_iterations = iterations;
        solve_config.resume_path = NULL;
        solve_config.checkpoint_path = NULL;
        solve_config.checkpoint_interval = 0;
        solve_config.checkpoint_final = 0;
        solve_config.monitor_fn = NULL;
        solve_config.monitor_user = NULL;
        solve_config.monitor_period = 0;
        solve_config.metrics_fn = NULL;
        solve_config.metrics_user = NULL;
        solve_config.metrics_buffer = NULL;
        solve_config.exploitability_interval = 0;
        solve_config.convergence_threshold = 0.0;
        started = clock();
        if (cfr_solve(&game, storage, &solve_config, &exploitability) < 0.0)
            rc = -1;
        elapsed = clock() - started;
    }
    if (rc == 0)
        rc = cfr_storage_export_delta(storage, &delta, &delta_size);
    if (rc == 0) {
        out_result->iterations = (uint64_t)iterations;
        out_result->infosets_trained =
            (uint64_t)cfr_storage_count_infosets(storage);
        out_result->exploitability = pe_finite_double(exploitability)
            ? exploitability : 0.0;
        out_result->worst_margin = 0.0;
        out_result->mean_margin = 0.0;
        out_result->constraints_satisfied = 1;
        out_result->elapsed_ns = elapsed > 0
            ? (uint64_t)(((double)elapsed * 1000000000.0) /
                         (double)CLOCKS_PER_SEC) : 1u;
        out_result->units_per_s = (double)iterations /
            ((double)out_result->elapsed_ns / 1000000000.0);
        out_result->delta = delta;
        out_result->delta_size = delta_size;
        out_result->delta_owned = 1;
        delta = NULL;
    }
    free(delta);
    cfr_storage_destroy(storage);
    if (config->destroy_game)
        config->destroy_game(&game, config->user_data);
    return rc;
}

int pe_cfr_work_reducer_apply(pe_work_reducer_t *reducer,
                              cfr_storage_t *destination,
                              size_t *out_applied)
{
    size_t i;
    size_t applied = 0u;
    if (out_applied)
        *out_applied = 0u;
    if (!reducer || !destination)
        return -1;
    /* The reducer is allowed to receive results in network arrival order. */
    pe_work_reducer_sort(reducer);
    for (i = 0u; i < pe_work_reducer_count(reducer); ++i) {
        const pe_work_result_record_t *record =
            pe_work_reducer_get(reducer, i);
        if (!record || (record->result.delta_size != 0u &&
                        cfr_storage_apply_delta(destination,
                                                record->result.delta,
                                                record->result.delta_size,
                                                1.0) != 0))
            return -1;
        if (record->result.delta_size != 0u)
            ++applied;
    }
    if (out_applied)
        *out_applied = applied;
    return 0;
}

static uint64_t read_be_mask(const uint8_t *bytes)
{
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < sizeof(value); ++i)
        value = (value << 8u) | (uint64_t)bytes[i];
    return value;
}

int pe_cfr_holdem_river_build_game(const pe_work_unit_t *unit,
                                   pe_compute_kind_t backend,
                                   cfr_game_t *out_game,
                                   void *user_data)
{
    const pe_cfr_holdem_river_work_context_t *context =
        (const pe_cfr_holdem_river_work_context_t *)user_data;
    holdem_river_state_t *state;
    mask_t h0;
    mask_t h1;
    mask_t board;
    if (!unit || !out_game || !context || !context->context ||
        unit->board_width != sizeof(uint64_t) || unit->board_count != 1u ||
        unit->ranges_size != 2u * sizeof(uint64_t))
        return -1;
    state = (holdem_river_state_t *)calloc(1u, sizeof(*state));
    if (!state)
        return -1;
    h0 = read_be_mask(unit->ranges);
    h1 = read_be_mask(unit->ranges + sizeof(uint64_t));
    board = read_be_mask(unit->boards);
    hr_build_game(context->context, h0, h1, board, out_game, state);
    /* Backend selection stays local to the worker; only the borrowed compute
     * port crosses into the legacy game's terminal callback. */
    state->compute_ops = context->compute_ops;
    state->compute_self = context->compute_self;
    (void)backend;
    return 0;
}

void pe_cfr_holdem_river_destroy_game(cfr_game_t *game, void *user_data)
{
    (void)user_data;
    if (game)
        free(game->initial_state);
    if (game)
        memset(game, 0, sizeof(*game));
}
