/* API-01: public lifecycle state and invalid-state contracts. */

#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_storage.h>
#include <poker_eval/solver/pe_traversal.h>

#include <math.h>
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
    return state == user
        ? (const void *)((const char *)user + 1 + action)
        : NULL;
}

static int values_one_step(const void *state, const pe_reach_vec_t *reach,
                           pe_value_vec_t *out_values, uint8_t players,
                           void *user)
{
    size_t combo;
    double value;
    (void)reach;
    (void)user;
    if (!state || !out_values || players != 2u)
        return -1;
    value = state == (const void *)((const char *)user + 1) ? 0.0 : 1.0;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = -value;
    }
    return 0;
}

/* Issue #249: the heartbeat publishes 0.0 while nothing has been measured.
 * What keeps that placeholder from reading as a converged zero is the
 * statement it carries with it, so pin the statement: every heartbeat taken
 * before a measurement must say `br_mode=unmeasured`, and none may omit it. */
typedef struct {
    int heartbeats;
    int stated_unmeasured;
    int missing_statement;
} heartbeat_capture_t;

static void heartbeat_cb(const pe_telemetry_event_t *event, void *user)
{
    heartbeat_capture_t *capture = (heartbeat_capture_t *)user;
    const char *line;
    if (capture == NULL || event == NULL || event->message == NULL)
        return;
    line = event->message;
    if (strstr(line, "progress iteration=") == NULL)
        return;
    capture->heartbeats++;
    if (strstr(line, " br_mode=unmeasured") != NULL)
        capture->stated_unmeasured++;
    else if (strstr(line, " br_mode=") == NULL)
        capture->missing_statement++;
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
        pe_solver_config_t precision_config = pe_solver_config_default();
        precision_config.execution.precision = PE_PREC_F32;
        precision_config.problem.expected_infosets = 1u;
        solver = pe_solver_create(&precision_config, NULL);
        CHECK(solver != NULL, "precision-configured solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_storage_precision(pe_solver_get_storage_instance(solver)) ==
                      PE_PREC_F32,
                  "default RAM storage must honor configured precision");
            pe_solver_destroy(solver);
        }
    }

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
        {
            heartbeat_capture_t capture = {0};
            pe_telemetry_callback_ctx_t context;
            pe_telemetry_ops_t ops;
            if (pe_telemetry_callback_init(&context, heartbeat_cb, &capture,
                                           PE_LOG_INFO) == 0)
            {
                ops = pe_telemetry_callback_ops(&context);
                deps.telemetry = &ops;
            }
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
            CHECK(capture.heartbeats > 0,
                  "the vector lane emitted no heartbeat to check");
            CHECK(capture.missing_statement == 0,
                  "%d heartbeat(s) published a value without saying whether it "
                  "was measured", capture.missing_statement);
            CHECK(capture.stated_unmeasured == capture.heartbeats,
                  "a heartbeat taken before any measurement must say "
                  "br_mode=unmeasured (%d of %d did)",
                  capture.stated_unmeasured, capture.heartbeats);
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

    {
        static char target_root;
        pe_solver_config_t target_config = pe_solver_config_default();
        pe_solver_deps_t deps = pe_solver_deps_default();
        pe_vector_game_t game;
        pe_progress_t target_progress;
        pe_metrics_t target_metrics;
        pe_best_response_vector_config_t br_config =
            pe_best_response_vector_config_default();
        pe_exploitability_vector_result_t direct = {0};

        memset(&game, 0, sizeof(game));
        game.root = &target_root;
        game.user = &target_root;
        game.player_count = 2u;
        game.combo_count = 1u;
        game.is_terminal = terminal_one_step;
        game.acting_player = acting_root;
        game.action_count = actions_one_step;
        game.infoset_key = key_one_step;
        game.apply_action = apply_one_step;
        game.terminal_values = values_one_step;
        target_config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
        target_config.max_iterations = 0u;
        target_config.target_exploitability_mbb = 600.0;
        target_config.exploitability_interval = 1u;
        target_config.problem.expected_infosets = 1u;
        target_config.problem.expected_actions = 2u;
        target_config.problem.expected_combos = 1u;
        deps.vector_game = &game;
        CHECK(pe_exploitability_vector(&game, &br_config, &direct) ==
                  PE_SOLVER_OK && direct.exploitability_raw > 0.0,
              "direct exploitability was %g (gaps %g/%g)",
              direct.exploitability_raw, direct.br_gap[0], direct.br_gap[1]);
        solver = pe_solver_create(&target_config, &deps);
        CHECK(solver != NULL, "target solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_solver_run(solver) == PE_SOLVER_OK,
                  "exploitability-target run failed");
            CHECK(pe_solver_progress(solver, &target_progress) == PE_SOLVER_OK &&
                      target_progress.complete && target_progress.iteration == 1u,
                  "target stop did not complete at iteration one");
            CHECK(pe_solver_metrics(solver, &target_metrics) == PE_SOLVER_OK &&
                      target_metrics.exploitability_raw > 0.0 &&
                      target_metrics.exploitability_mbb_per_game <= 600.0,
                  "target metrics were not recorded (raw=%g mbb=%g)",
                  target_metrics.exploitability_raw,
                  target_metrics.exploitability_mbb_per_game);
            /* Issue #235: metrics carry the storage's memory accounting. The
               one-infoset solve must report exactly one infoset, non-zero
               storage bytes and a consistent bytes-per-infoset figure. */
            CHECK(target_metrics.storage_memory.total_infosets == 1u,
                  "storage memory reported %zu infosets, expected 1",
                  target_metrics.storage_memory.total_infosets);
            CHECK(target_metrics.storage_memory.storage_bytes > 0u &&
                      target_metrics.storage_memory.bytes_per_infoset > 0.0,
                  "storage memory reported no bytes");
            CHECK(fabs(target_metrics.storage_memory.bytes_per_infoset -
                       (double)target_metrics.storage_memory.storage_bytes /
                           (double)target_metrics.storage_memory.total_infosets) <
                      0.01,
                  "bytes_per_infoset does not follow from storage_bytes");
            pe_solver_destroy(solver);
        }
    }

    {
        static char measured_root;
        pe_solver_config_t measured_config = pe_solver_config_default();
        pe_solver_deps_t measured_deps = pe_solver_deps_default();
        pe_vector_game_t measured_game;
        pe_exploitability_vector_result_t direct = {0};
        pe_best_response_vector_config_t br_config =
            pe_best_response_vector_config_default();
        pe_metrics_t measured_metrics;

        memset(&measured_game, 0, sizeof(measured_game));
        measured_game.root = &measured_root;
        measured_game.user = &measured_root;
        measured_game.player_count = 2u;
        measured_game.combo_count = 1u;
        measured_game.is_terminal = terminal_one_step;
        measured_game.acting_player = acting_root;
        measured_game.action_count = actions_one_step;
        measured_game.infoset_key = key_one_step;
        measured_game.apply_action = apply_one_step;
        measured_game.terminal_values = values_one_step;
        measured_config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
        measured_config.max_iterations = 2u;
        measured_config.exploitability_interval = 1u;
        measured_config.problem.expected_infosets = 1u;
        measured_config.problem.expected_actions = 2u;
        measured_config.problem.expected_combos = 1u;
        measured_deps.vector_game = &measured_game;

        CHECK(pe_exploitability_vector(&measured_game, &br_config, &direct) ==
                  PE_SOLVER_OK,
              "direct baseline exploitability failed");
        solver = pe_solver_create(&measured_config, &measured_deps);
        CHECK(solver != NULL, "metric solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_solver_run(solver) == PE_SOLVER_OK,
                  "vector metric run failed");
            CHECK(pe_solver_metrics(solver, &measured_metrics) == PE_SOLVER_OK,
                  "vector metrics were not measured without a target");
            CHECK(fabs(measured_metrics.exploitability_raw -
                       direct.exploitability_raw) > 1e-12,
                  "vector metrics still measured the uniform policy");
            pe_solver_destroy(solver);
        }
    }
    {
        /* Issue #234: the worst-player target stops the loop too, and the
           measurement metadata stamps the iteration that produced it. */
        static char gap_target_root;
        pe_solver_config_t gap_config = pe_solver_config_default();
        pe_solver_deps_t deps = pe_solver_deps_default();
        pe_vector_game_t game;
        pe_progress_t gap_progress;
        pe_metrics_t gap_metrics;

        memset(&game, 0, sizeof(game));
        game.root = &gap_target_root;
        game.user = &gap_target_root;
        game.player_count = 2u;
        game.combo_count = 1u;
        game.is_terminal = terminal_one_step;
        game.acting_player = acting_root;
        game.action_count = actions_one_step;
        game.infoset_key = key_one_step;
        game.apply_action = apply_one_step;
        game.terminal_values = values_one_step;
        gap_config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
        gap_config.max_iterations = 0u;
        gap_config.target_max_br_gap_mbb = 600.0;
        gap_config.exploitability_interval = 1u;
        gap_config.problem.expected_infosets = 1u;
        gap_config.problem.expected_actions = 2u;
        gap_config.problem.expected_combos = 1u;
        deps.vector_game = &game;
        solver = pe_solver_create(&gap_config, &deps);
        CHECK(solver != NULL, "gap-target solver creation failed");
        if (solver != NULL)
        {
            CHECK(pe_solver_run(solver) == PE_SOLVER_OK,
                  "max-br-gap target run failed");
            CHECK(pe_solver_progress(solver, &gap_progress) == PE_SOLVER_OK &&
                      gap_progress.complete && gap_progress.iteration == 1u,
                  "max-br-gap stop did not complete at iteration one");
            CHECK(pe_solver_metrics(solver, &gap_metrics) == PE_SOLVER_OK &&
                      gap_metrics.max_br_gap > 0.0 &&
                      gap_metrics.max_br_gap / gap_metrics.big_blind * 1000.0 <=
                          600.0 &&
                      gap_metrics.measurement_iteration == 1u &&
                      gap_metrics.sample_count == 0u &&
                      gap_metrics.br_mode == PE_BR_EXACT,
                  "gap-target metrics were not recorded "
                  "(max=%g iter=%llu)",
                  gap_metrics.max_br_gap,
                  (unsigned long long)gap_metrics.measurement_iteration);
            pe_solver_destroy(solver);
        }
    }
    return failures != 0;
}
