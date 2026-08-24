/* Lane B integration: the public solver lifecycle drives external sampling. */

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_external_best_response.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <stdint.h>
#include <stdio.h>

static int terminal(const void *state, void *user)
{
    (void)user;
    return (uintptr_t)state >= 100u;
}

static int acting(const void *state, void *user)
{
    (void)user;
    if ((uintptr_t)state == 10u) return 0;
    if ((uintptr_t)state == 20u || (uintptr_t)state == 30u) return 1;
    return -1;
}

static uint16_t actions(const void *state, void *user)
{
    (void)user;
    return ((uintptr_t)state == 10u || (uintptr_t)state == 20u ||
            (uintptr_t)state == 30u) ? 2u : 0u;
}

static uint64_t key(const void *state, void *user)
{
    (void)user;
    return (uint64_t)(uintptr_t)state;
}

static const void *apply_action(const void *state, uint16_t action, void *user)
{
    (void)user;
    if ((uintptr_t)state == 10u)
        return (const void *)(uintptr_t)(action == 0u ? 20u : 30u);
    if ((uintptr_t)state == 20u)
        return (const void *)(uintptr_t)(100u + action);
    if ((uintptr_t)state == 30u)
        return (const void *)(uintptr_t)(110u + action);
    return NULL;
}

static double probability(const void *state, uint64_t infoset,
                          uint16_t action, void *user)
{
    (void)state; (void)infoset; (void)action; (void)user;
    return 0.5;
}

static double utility(const void *state, int player, void *user)
{
    (void)user;
    if (player != 0) return 0.0;
    return (uintptr_t)state < 110u ? 1.0 : -1.0;
}

static int sample_chance_with_user(const void *state, pe_rng_t *rng,
                                   pe_chance_sample_t *out, void *user)
{
    (void)user;
    if ((uintptr_t)state != 50u || !rng || !out) return -1;
    out->outcome = (int)pe_rng_below(rng, 2u);
    out->importance_ratio = 1.0;
    return 0;
}

static const void *apply_chance(const void *state, int outcome, void *user)
{
    (void)user;
    if ((uintptr_t)state != 50u || outcome < 0 || outcome > 1) return NULL;
    return (const void *)(uintptr_t)10u;
}

int main(void)
{
    pe_external_game_t game = {0};
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_progress_t progress;
    pe_metrics_t metrics;
    pe_external_br_config_t br_config = pe_external_br_config_default();
    pe_external_br_result_t br_result;

    game.root = (const void *)(uintptr_t)50u;
    game.player_count = 2u;
    game.is_terminal = terminal;
    game.acting_player = acting;
    game.action_count = actions;
    game.infoset_key = key;
    game.apply_action = apply_action;
    game.action_probability = probability;
    game.terminal_value = utility;
    game.sample_chance_with_user = sample_chance_with_user;
    game.apply_chance = apply_chance;

    br_config.samples = 64u;
    br_config.max_depth = 16u;
    br_config.seed = 0x55u;
    if (pe_external_best_response_sampled(&game, 0u, &br_config, &br_result) != 0 ||
        !br_result.empirical || br_result.policy_samples == 0u ||
        br_result.br_samples == 0u || br_result.br_gap < 0.0)
    {
        fprintf(stderr, "test_pe_solver_sampled: empirical BR failed\n");
        return 1;
    }

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.max_iterations = 64u;
    cfg.problem.expected_infosets = 4u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.seed = 0x1234u;
    cfg.target_exploitability_mbb = 1.0;
    cfg.exploitability_interval = 16u;
    deps.external_game = &game;
    solver = pe_solver_create(&cfg, &deps);
    if (!solver || pe_solver_run(solver) != PE_SOLVER_OK ||
        pe_solver_progress(solver, &progress) != PE_SOLVER_OK ||
        !progress.complete || progress.iteration != cfg.max_iterations)
    {
        fprintf(stderr, "test_pe_solver_sampled: Lane B lifecycle failed\n");
        pe_solver_destroy(solver);
        return 1;
    }
    if (pe_solver_metrics(solver, &metrics) != PE_SOLVER_OK ||
        metrics.guarantee != PE_GUARANTEE_EMPIRICAL ||
        metrics.exploitability_mbb_per_game < 0.0)
    {
        fprintf(stderr, "test_pe_solver_sampled: Lane B metrics failed\n");
        pe_solver_destroy(solver);
        return 1;
    }
    pe_solver_destroy(solver);
    puts("test_pe_solver_sampled: external sampling lifecycle passed");
    return 0;
}
