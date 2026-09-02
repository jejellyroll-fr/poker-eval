#include <poker_eval/engine/solvers/cfr/legacy_vector_adapter.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int terminal;
    int action;
} state_t;

static int is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return ((state_t *)(uintptr_t)key)->terminal;
}

static int current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)key;
    (void)user;
    return 0;
}

static int get_actions(cfr_game_t *game, uint64_t key, int *out, int max,
                       void *user)
{
    (void)game;
    (void)key;
    (void)user;
    if (max < 2)
        return 0;
    /* Legacy action identifiers are values, not vector ordinals. */
    out[0] = 3;
    out[1] = 7;
    return 2;
}

static uint64_t apply_action(cfr_game_t *game, uint64_t key, int action,
                             void *user)
{
    state_t *next;
    (void)game;
    (void)key;
    (void)user;
    next = (state_t *)calloc(1u, sizeof(*next));
    if (!next)
        return 0u;
    next->terminal = 1;
    next->action = action;
    return (uint64_t)(uintptr_t)next;
}

static uint64_t infoset_key(const void *state)
{
    (void)state;
    return 11u;
}

static double get_utility(cfr_game_t *game, uint64_t key, int player,
                          void *user)
{
    state_t *state = (state_t *)(uintptr_t)key;
    (void)game;
    (void)user;
    return player == 0 ? (state->action == 3 ? 1.0 : -1.0)
                       : (state->action == 3 ? -1.0 : 1.0);
}

static void release_state(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    free((void *)(uintptr_t)key);
}

int main(void)
{
    state_t root = {0, -1};
    cfr_game_t legacy;
    pe_legacy_vector_adapter_t adapter;
    pe_solver_config_t config = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_progress_t progress;
    memset(&legacy, 0, sizeof(legacy));
    legacy.current_player = current_player;
    legacy.get_actions = get_actions;
    legacy.apply_action = apply_action;
    legacy.get_infoset_key = infoset_key;
    legacy.is_terminal = is_terminal;
    legacy.get_utility = get_utility;
    legacy.release_state = release_state;
    legacy.initial_state = &root;
    legacy.num_players = 2;

    if (pe_legacy_vector_adapter_init(&adapter, &legacy, 1u) != 0)
        return 1;
    config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
    config.max_iterations = 32u;
    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 1u;
    deps.vector_game = pe_legacy_vector_adapter_game(&adapter);
    solver = pe_solver_create(&config, &deps);
    if (!solver || pe_solver_run(solver) != PE_SOLVER_OK ||
        pe_solver_progress(solver, &progress) != PE_SOLVER_OK ||
        !progress.complete || progress.iteration != 32u) {
        fprintf(stderr, "legacy vector adapter regression failed\n");
        pe_solver_destroy(solver);
        pe_legacy_vector_adapter_destroy(&adapter);
        return 1;
    }
    /* Legacy action 3 strictly dominates in this game; the bridged solve
     * must map vector slot 0 back to it, not merely complete. */
    {
        pe_strategy_query_t query;
        pe_strategy_view_t view;
        memset(&query, 0, sizeof(query));
        if (pe_solver_strategy(solver, &query, &view) != PE_SOLVER_OK ||
            view.action_count != 2u || view.combo_count != 1u ||
            view.count != 2u) {
            fprintf(stderr, "legacy vector adapter strategy query failed\n");
            pe_solver_destroy(solver);
            pe_legacy_vector_adapter_destroy(&adapter);
            return 1;
        }
        if (fabs(view.values[0] + view.values[1] - 1.0) > 1e-9 ||
            view.values[0] < 0.9 || view.values[0] <= view.values[1]) {
            fprintf(stderr,
                    "legacy vector adapter did not converge to the dominant "
                    "action (got %.6f / %.6f)\n",
                    view.values[0], view.values[1]);
            pe_solver_destroy(solver);
            pe_legacy_vector_adapter_destroy(&adapter);
            return 1;
        }
    }
    pe_solver_destroy(solver);

    /* Full-tree presets must all use the same exact legacy-to-vector bridge.
     * In particular, CFR and ECFR declare FULL_SCALAR while CFR+ and DCFR
     * declare FULL_VECTOR; neither is allowed to stop at a configuration-only
     * validation path. */
    {
        const pe_algorithm_preset_t presets[] = {
            PE_PRESET_CFR,
            PE_PRESET_CFR_PLUS,
            PE_PRESET_DCFR,
            PE_PRESET_ECFR
        };
        size_t i;
        for (i = 0u; i < sizeof(presets) / sizeof(presets[0]); ++i)
        {
            pe_solver_config_t preset_config = pe_solver_config_default();
            pe_solver_deps_t preset_deps = pe_solver_deps_default();
            pe_solver_t *preset_solver;
            pe_progress_t preset_progress;
            preset_config.algorithm.preset = presets[i];
            preset_config.max_iterations = 2u;
            preset_config.problem.expected_infosets = 1u;
            preset_config.problem.expected_actions = 2u;
            preset_config.problem.expected_combos = 1u;
            preset_deps.vector_game = pe_legacy_vector_adapter_game(&adapter);
            preset_solver = pe_solver_create(&preset_config, &preset_deps);
            if (!preset_solver ||
                pe_solver_run(preset_solver) != PE_SOLVER_OK ||
                pe_solver_progress(preset_solver, &preset_progress) != PE_SOLVER_OK ||
                !preset_progress.complete || preset_progress.iteration != 2u)
            {
                fprintf(stderr, "legacy adapter preset %s did not run full-tree\n",
                        pe_preset_name(presets[i]));
                pe_solver_destroy(preset_solver);
                pe_legacy_vector_adapter_destroy(&adapter);
                return 1;
            }
            pe_solver_destroy(preset_solver);
        }
    }
    pe_legacy_vector_adapter_destroy(&adapter);
    return 0;
}
