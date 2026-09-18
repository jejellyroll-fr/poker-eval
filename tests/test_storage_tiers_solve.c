/*
 * test_storage_tiers_solve.c - ISS-235 (phase 6): storage tiers never touch
 * strategy results
 *
 * The tier contract: under the same precision and seed, FULL, COMPACT and
 * RECOMPUTE_DEEP must produce bitwise-identical convergence aggregates,
 * because dropping derived decoded state and re-decoding it byte-exact is
 * not an approximation. The test also pins that the drop passes actually
 * ran (recompute accounting) and that the tier is visible on the metrics.
 */

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_rng.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>

#include <math.h>
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

/* One seeded solve under the requested precision and tier; returns 0 on
 * success and fills *out with the named aggregates. */
static int run_solve(pe_precision_mode_t precision, pe_storage_policy_t tier,
                     double *out_exploitability, double *out_nash_conv,
                     uint64_t *out_recompute_calls,
                     pe_storage_policy_t *out_policy_seen)
{
    pe_external_game_t game = {0};
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_progress_t progress;
    pe_metrics_t metrics;

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

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.max_iterations = 64u;
    cfg.problem.expected_infosets = 4u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.execution.sample_batch_size = 4u;
    cfg.execution.precision = precision;
    cfg.execution.storage_policy = tier;
    cfg.seed = 0x1234u;
    cfg.target_exploitability_mbb = 0.0;
    cfg.exploitability_interval = 16u;
    deps.external_game = &game;

    solver = pe_solver_create(&cfg, &deps);
    if (!solver || pe_solver_run(solver) != PE_SOLVER_OK ||
        pe_solver_progress(solver, &progress) != PE_SOLVER_OK ||
        !progress.complete || progress.iteration != cfg.max_iterations ||
        pe_solver_metrics(solver, &metrics) != PE_SOLVER_OK)
    {
        fprintf(stderr, "tiered solve failed (tier %d)\n", (int)tier);
        pe_solver_destroy(solver);
        return -1;
    }
    *out_exploitability = metrics.exploitability_raw;
    *out_nash_conv = metrics.nash_conv;
    *out_recompute_calls = metrics.recompute_calls;
    *out_policy_seen = metrics.storage_memory_policy;
    pe_solver_destroy(solver);
    return 0;
}

int main(void)
{
    double expl[3], nash[3];
    uint64_t recomputes[3];
    pe_storage_policy_t policies[3];
    const pe_storage_policy_t tiers[3] = {
        PE_STORAGE_FULL, PE_STORAGE_COMPACT, PE_STORAGE_RECOMPUTE_DEEP
    };
    const pe_precision_mode_t precisions[2] = {PE_PREC_F64, PE_PREC_F32};

    for (int p = 0; p < 2; ++p)
    {
        for (int t = 0; t < 3; ++t)
            if (run_solve(precisions[p], tiers[t], &expl[t], &nash[t],
                          &recomputes[t], &policies[t]) != 0)
                return 1;

        /* The contract: identical seeds plus identical precision means
           identical aggregates, bitwise, whatever the tier. */
        for (int t = 1; t < 3; ++t)
        {
            if (expl[t] != expl[0] || nash[t] != nash[0])
            {
                fprintf(stderr,
                        "tier %d changed aggregates (expl %g vs %g, nash %g vs %g)\n",
                        (int)tiers[t], expl[t], expl[0], nash[t], nash[0]);
                return 1;
            }
        }
        /* The tier that ran is whatever was configured, never inferred. */
        for (int t = 0; t < 3; ++t)
            if (policies[t] != tiers[t])
            {
                fprintf(stderr, "metrics report tier %d, configured %d\n",
                        (int)policies[t], (int)tiers[t]);
                return 1;
            }
        /* Under F64 no derived layer exists anywhere, so no drop pass ever
           removes anything. */
        if (precisions[p] == PE_PREC_F64)
        {
            for (int t = 0; t < 3; ++t)
                if (recomputes[t] != 0)
                {
                    fprintf(stderr,
                            "F64 tier %d invented recomputable state (%llu)\n",
                            (int)tiers[t],
                            (unsigned long long)recomputes[t]);
                    return 1;
                }
        }
        else
        {
            /* Under F32 the recompute-deep solve walked the drop machinery:
               every iteration dropped spans and later re-materialised them. */
            if (recomputes[2] == 0)
            {
                fprintf(stderr,
                        "F32 RECOMPUTE_DEEP ran no re-materialisation\n");
                return 1;
            }
        }
    }

    puts("test_storage_tiers_solve: tiers preserve strategy results");
    return 0;
}
