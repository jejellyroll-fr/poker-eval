#include <poker_eval_api.h>

#include <stdio.h>
#include <string.h>

static int terminal(const void* state, void* user)
{
    (void)user;
    return state != NULL && state != user;
}

static int acting(const void* state, void* user)
{
    (void)state;
    (void)user;
    return 0u;
}

static uint16_t actions(const void* state, void* user)
{
    (void)state;
    (void)user;
    return 2u;
}

static const void* apply_action(const void* state, uint16_t action, void* user)
{
    (void)action;
    return state == user ? (const void*)((const char*)user + 1) : NULL;
}

static uint64_t infoset_key(const void* state, void* user)
{
    (void)state;
    (void)user;
    return 1u;
}

static int terminal_values(const void* state, const pe_reach_vec_t* reach,
                           pe_value_vec_t* out_values, uint8_t players,
                           void* user)
{
    size_t combo;
    double value;
    (void)reach;
    if (!state || !out_values || players != 2u)
        return -1;
    value = state == (const void*)((const char*)user + 1) ? 1.0 : -1.0;
    for (combo = 0u; combo < out_values[0].n; ++combo) {
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = -value;
    }
    return 0;
}

int main(void)
{
    static char root;
    pe_vector_game_t game;
    pe_solver_config_t config = pe_solver_config_default();
    pe_solver_api_handle_t solver;
    pe_progress_t progress;
    pe_strategy_query_t query = {0u};
    pe_strategy_view_t view;

    memset(&game, 0, sizeof(game));
    game.root = &root;
    game.user = &root;
    game.player_count = 2u;
    game.combo_count = 1u;
    game.is_terminal = terminal;
    game.acting_player = acting;
    game.action_count = actions;
    game.apply_action = apply_action;
    game.infoset_key = infoset_key;
    game.terminal_values = terminal_values;

    config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
    config.max_iterations = 1u;
    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 1u;

    solver = pe_solver_api_create(&config, &game);
    if (!solver || pe_solver_api_validate(solver, NULL) != PE_SOLVER_OK ||
        pe_solver_api_run(solver) != PE_SOLVER_OK ||
        pe_solver_api_progress(solver, &progress) != PE_SOLVER_OK ||
        !progress.complete || progress.iteration != 1u ||
        pe_solver_api_strategy(solver, &query, &view) != PE_SOLVER_OK ||
        view.action_count != 2u || view.combo_count != 1u ||
        view.count != 2u) {
        fprintf(stderr, "v3 C façade regression failed\n");
        pe_solver_api_free(solver);
        return 1;
    }

    pe_solver_api_free(solver);
    return 0;
}
