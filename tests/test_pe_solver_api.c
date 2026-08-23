/* API-01: public lifecycle state and invalid-state contracts. */

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stdio.h>
#include <string.h>

static int failures;

static int terminal_root(const void *state, void *user)
{
    return state == user;
}

static int acting_root(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t actions_root(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static const void *apply_root(const void *state, uint16_t action, void *user)
{
    (void)state;
    (void)action;
    (void)user;
    return NULL;
}

static int terminal_one_step(const void *state, void *user)
{
    return state != user;
}

static uint16_t actions_one_step(const void *state, void *user)
{
    return state == user ? 2u : 0u;
}

static uint64_t key_one_step(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0x1234u;
}

static const void *apply_one_step(const void *state, uint16_t action, void *user)
{
    (void)action;
    return state == user ? (const void *)((const char *)user + 1) : NULL;
}

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                             \
        }                                                          \
    } while (0)

int main(void)
{
    pe_solver_config_t config = pe_solver_config_default();
    pe_solver_t *solver;
    pe_progress_t progress;
    pe_metrics_t metrics;
    pe_strategy_query_t query = {0u};
    pe_strategy_view_t view;
    uint64_t caps = 0u;

    config.problem.expected_infosets = 100u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 1u;
    solver = pe_solver_create(&config, NULL);
    CHECK(solver != NULL, "solver creation failed");
    if (solver == NULL)
        return 1;

    CHECK(pe_solver_progress(solver, &progress) == PE_SOLVER_ERR_INVALID_STATE,
          "progress before validation should be an invalid state");
    CHECK(pe_solver_pause(solver) == PE_SOLVER_ERR_INVALID_STATE,
          "pause before run should be an invalid state");
    CHECK(pe_solver_resume(solver) == PE_SOLVER_ERR_INVALID_STATE,
          "resume before pause should be an invalid state");
    CHECK(pe_solver_stop(solver) == PE_SOLVER_ERR_INVALID_STATE,
          "stop before run should be an invalid state");
    CHECK(pe_solver_strategy(solver, &query, &view) == PE_SOLVER_ERR_INVALID_STATE,
          "strategy before run should be an invalid state");
    CHECK(pe_solver_metrics(solver, &metrics) == PE_SOLVER_ERR_INVALID_STATE,
          "metrics before run should be an invalid state");
    CHECK(pe_solver_validate(solver, NULL) == PE_SOLVER_OK,
          "validation failed");
    CHECK(pe_solver_capabilities(solver, &caps) == PE_SOLVER_OK && caps != 0u,
          "capability query failed after validation");
    CHECK(pe_solver_get_storage(solver) != NULL &&
              strcmp(pe_solver_get_storage(solver)->name, "ram") == 0,
          "solver should resolve the default RAM storage adapter");
    CHECK(pe_solver_plan(solver, NULL) == PE_SOLVER_ERR_NULL_ARGUMENT,
          "plan must reject a NULL output");
    CHECK(pe_solver_run(solver) == PE_SOLVER_ERR_NOT_IMPLEMENTED,
          "valid run should remain explicit until the driver is wired");
    CHECK(pe_solver_progress(solver, &progress) == PE_SOLVER_OK &&
              !progress.running && !progress.complete &&
              progress.iteration == 0u && progress.total_iterations == config.max_iterations &&
              progress.fraction == 0.0,
          "post-run progress snapshot is inconsistent");
    CHECK(pe_solver_run(solver) == PE_SOLVER_ERR_INVALID_STATE,
          "a solver must reject a second run attempt");
    CHECK(pe_solver_stop(solver) == PE_SOLVER_ERR_INVALID_STATE,
          "stop after an unimplemented run should not claim a running solve");
    CHECK(pe_solver_save(solver, NULL) == PE_SOLVER_ERR_NULL_ARGUMENT,
          "save must reject a NULL target");
    CHECK(pe_solver_load(solver, NULL) == PE_SOLVER_ERR_NULL_ARGUMENT,
          "load must reject a NULL source");
    pe_solver_destroy(solver);

    {
        static int root;
        pe_solver_config_t vector_config = pe_solver_config_default();
        pe_solver_deps_t deps = pe_solver_deps_default();
        pe_vector_game_t game;
        pe_progress_t vector_progress;

        memset(&game, 0, sizeof(game));
        game.root = &root;
        game.user = &root;
        game.player_count = 2u;
        game.combo_count = 1u;
        game.is_terminal = terminal_root;
        game.acting_player = acting_root;
        game.action_count = actions_root;
        game.apply_action = apply_root;
        vector_config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
        vector_config.max_iterations = 3u;
        vector_config.problem.expected_infosets = 1u;
        vector_config.problem.expected_actions = 1u;
        vector_config.problem.expected_combos = 1u;
        deps.vector_game = &game;

        solver = pe_solver_create(&vector_config, &deps);
        CHECK(solver != NULL, "vector solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_solver_run(solver) == PE_SOLVER_OK,
                  "vector solver run failed");
            CHECK(pe_solver_progress(solver, &vector_progress) == PE_SOLVER_OK &&
                      vector_progress.iteration == 3u &&
                      vector_progress.total_iterations == 3u &&
                      vector_progress.fraction == 1.0 &&
                      vector_progress.complete,
                  "vector solver progress snapshot is inconsistent");
            pe_solver_destroy(solver);
        }
    }

    {
        static char one_step_root;
        pe_solver_config_t vector_config = pe_solver_config_default();
        pe_solver_deps_t deps = pe_solver_deps_default();
        pe_vector_game_t game;
        pe_strategy_query_t strategy_query = {0u};
        pe_strategy_view_t strategy_view;

        memset(&game, 0, sizeof(game));
        game.root = &one_step_root;
        game.user = &one_step_root;
        game.player_count = 2u;
        game.combo_count = 1u;
        game.is_terminal = terminal_one_step;
        game.acting_player = acting_root;
        game.action_count = actions_one_step;
        game.infoset_key = key_one_step;
        game.apply_action = apply_one_step;
        vector_config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
        vector_config.max_iterations = 1u;
        vector_config.problem.expected_infosets = 1u;
        vector_config.problem.expected_actions = 2u;
        vector_config.problem.expected_combos = 1u;
        deps.vector_game = &game;

        solver = pe_solver_create(&vector_config, &deps);
        CHECK(solver != NULL, "storage-backed vector solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_solver_run(solver) == PE_SOLVER_OK,
                  "storage-backed vector solver run failed");
            CHECK(pe_solver_strategy(solver, &strategy_query, &strategy_view) ==
                      PE_SOLVER_OK && strategy_view.count == 2u &&
                      strategy_view.action_count == 2u &&
                      strategy_view.combo_count == 1u &&
                      strategy_view.values[0] == 0.5 &&
                      strategy_view.values[1] == 0.5,
                  "strategy query should normalize the stored average per combo");
            pe_solver_destroy(solver);
        }
    }
    return failures != 0;
}
