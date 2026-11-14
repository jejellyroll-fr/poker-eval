/*
 * test_pe_dcfr_schedule.c - the DCFR schedule reaches the v3 solve (issue #284)
 *
 * The schedule unit test pins the equations. This one pins the wiring: it runs
 * the public v3 solver on a three-infoset toy game with the same seed and
 * compares the average strategies it materialises.
 *
 *   fixed, default config  vs fixed, explicit exponents at horizon 1
 *       -> bitwise identical: the new fields do not change the control
 *   fixed vs hs30, both at horizon 1
 *       -> different trajectory: the schedule reaches the update rule
 *   hs30 at horizon 1 vs horizon 1000
 *       -> different trajectory: the horizon is not ignored
 *
 * The toy game is asymmetric so both signs of regret occur at every infoset:
 * a discount applied uniformly to all actions would cancel in regret matching,
 * while the schedules scale positive and negative regrets differently.
 */

#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* root (player 0, state 1) -> 10 or 20 (player 1) -> terminals 100..111.
   Infoset keys are the state values, so the game has three infosets. */
static int toy_terminal(const void *state, void *user)
{
    (void)user;
    return (uintptr_t)state >= 100u;
}

static int toy_acting(const void *state, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (value == 1u)
        return 0;
    if (value == 10u || value == 20u)
        return 1;
    return -1;
}

static uint16_t toy_actions(const void *state, void *user)
{
    (void)user;
    return toy_terminal(state, user) ? 0u : 2u;
}

static uint64_t toy_key(const void *state, void *user)
{
    (void)user;
    return (uint64_t)(uintptr_t)state;
}

static const void *toy_apply(const void *state, uint16_t action, void *user)
{
    uintptr_t value = (uintptr_t)state;
    (void)user;
    if (action > 1u)
        return NULL;
    if (value == 1u)
        return (const void *)(uintptr_t)(action == 0u ? 10u : 20u);
    if (value == 10u)
        return (const void *)(uintptr_t)(100u + action);
    if (value == 20u)
        return (const void *)(uintptr_t)(110u + action);
    return NULL;
}

/* +1/-1 for player 0, mirrored for player 1; the branches disagree, so
   neither player has a dominant action at either infoset. */
static int toy_values(const void *state, const pe_reach_vec_t *reach,
                      pe_value_vec_t *out_values, uint8_t players, void *user)
{
    double value;
    pe_combo_id_t combo;
    (void)reach;
    (void)user;

    if (!out_values || players != 2u)
        return -1;
    switch ((uintptr_t)state)
    {
    case 100u: value = 1.0; break;
    case 101u: value = -1.0; break;
    case 110u: value = -1.0; break;
    case 111u: value = 1.0; break;
    default: return -1;
    }
    for (combo = 0; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = -value;
    }
    return 0;
}

typedef struct
{
    double exploitability;
    double strategy[16];
    size_t strategy_count;
} toy_run_t;

/* `use_default_config` leaves alpha/beta/gamma and the new schedule fields at
   their defaults, which is what a caller written before issue #284 does. The
   other runs spell out the legacy fixed exponents explicitly. */
static int solve_toy(pe_dcfr_schedule_t schedule, uint64_t horizon,
                     int use_default_config, uint64_t seed,
                     uint32_t iterations, toy_run_t *out)
{
    pe_vector_game_t game;
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_progress_t progress = {0};
    pe_metrics_t metrics;
    size_t infoset;
    size_t total = 0;

    memset(&game, 0, sizeof(game));
    /* Small integer states: 1 is the root, 100+ are terminals. */
    game.root = (const void *)(uintptr_t)1u;
    game.player_count = 2u;
    game.combo_count = 1u;
    game.is_terminal = toy_terminal;
    game.acting_player = toy_acting;
    game.action_count = toy_actions;
    game.infoset_key = toy_key;
    game.apply_action = toy_apply;
    game.terminal_values = toy_values;

    cfg.algorithm.preset = PE_PRESET_DCFR;
    if (!use_default_config)
    {
        cfg.algorithm.dcfr_alpha = 1.5;
        cfg.algorithm.dcfr_beta = 0.0;
        cfg.algorithm.dcfr_gamma = 2.0;
        cfg.algorithm.dcfr_schedule = schedule;
        cfg.algorithm.dcfr_schedule_horizon = horizon;
    }
    cfg.max_iterations = iterations;
    cfg.problem.expected_infosets = 3u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.seed = seed;
    cfg.target_exploitability_mbb = 0.0;
    cfg.exploitability_interval = 1u;
    deps.vector_game = &game;

    memset(out, 0, sizeof(*out));
    solver = pe_solver_create(&cfg, &deps);
    if (!solver)
        return -1;
    if (pe_solver_run(solver) != PE_SOLVER_OK ||
        pe_solver_progress(solver, &progress) != PE_SOLVER_OK ||
        !progress.complete || progress.iteration != iterations ||
        pe_solver_metrics(solver, &metrics) != PE_SOLVER_OK)
    {
        pe_solver_destroy(solver);
        return -1;
    }
    out->exploitability = metrics.exploitability_raw;

    for (infoset = 0; infoset < pe_solver_strategy_count(solver); ++infoset)
    {
        pe_strategy_query_t query = {0u};
        pe_strategy_view_t view;
        size_t value;

        query.infoset = (uint32_t)infoset;
        if (pe_solver_strategy(solver, &query, &view) != PE_SOLVER_OK ||
            total + view.count > sizeof(out->strategy) / sizeof(double))
        {
            pe_solver_destroy(solver);
            return -1;
        }
        for (value = 0; value < view.count; ++value)
            out->strategy[total++] = view.values[value];
    }
    out->strategy_count = total;
    pe_solver_destroy(solver);
    return 0;
}

/* Bitwise equality: this test is about the same trajectory, not about two
   solves landing within a tolerance of each other. */
static int same_run(const toy_run_t *left, const toy_run_t *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void dump(const char *label, const toy_run_t *run)
{
    size_t i;
    fprintf(stderr, "  %s: exploitability=%.17g strategy[%zu]=", label,
            run->exploitability, run->strategy_count);
    for (i = 0; i < run->strategy_count; ++i)
        fprintf(stderr, " %.17g", run->strategy[i]);
    fputc('\n', stderr);
}

int main(void)
{
    const uint32_t iterations = 8u;
    const uint64_t seed = 0x5EEDu;
    toy_run_t fixed_default;
    toy_run_t fixed_explicit;
    toy_run_t hs30_short;
    toy_run_t hs30_long;

    if (solve_toy(PE_DCFR_SCHEDULE_FIXED, 1000u, 1, seed, iterations,
                  &fixed_default) != 0 ||
        solve_toy(PE_DCFR_SCHEDULE_FIXED, 1u, 0, seed, iterations,
                  &fixed_explicit) != 0 ||
        solve_toy(PE_DCFR_SCHEDULE_HS30, 1u, 0, seed, iterations,
                  &hs30_short) != 0 ||
        solve_toy(PE_DCFR_SCHEDULE_HS30, 1000u, 0, seed, iterations,
                  &hs30_long) != 0)
    {
        fprintf(stderr, "test_pe_dcfr_schedule: a toy solve failed\n");
        return 1;
    }

    /* Guard against a vacuous comparison: both sides must have materialised
       the same non-empty set of strategies. */
    CHECK(fixed_explicit.strategy_count > 0u &&
              fixed_explicit.strategy_count == hs30_short.strategy_count,
          "the solves did not materialise comparable strategies");

    /* The control: a pre-#284 caller (default config) and an explicit fixed
       DCFR at a different horizon must take the same path. */
    CHECK(same_run(&fixed_default, &fixed_explicit),
          "the fixed DCFR path changed with the new fields");

    /* Same seed, same horizon, only the schedule differs. */
    CHECK(!same_run(&fixed_explicit, &hs30_short),
          "hs30 and fixed produced the same trajectory");

    /* Same schedule and seed, only the horizon differs. */
    CHECK(!same_run(&hs30_short, &hs30_long),
          "hs30 ignored dcfr_schedule_horizon");

    if (failures != 0)
    {
        dump("fixed default", &fixed_default);
        dump("fixed horizon=1", &fixed_explicit);
        dump("hs30 horizon=1", &hs30_short);
        dump("hs30 horizon=1000", &hs30_long);
        fprintf(stderr, "test_pe_dcfr_schedule: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_pe_dcfr_schedule: the DCFR schedule changes the solved strategy");
    return 0;
}
