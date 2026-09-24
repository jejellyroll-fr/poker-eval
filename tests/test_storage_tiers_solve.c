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
#include <poker_eval/solver/pe_storage.h>

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

/* The toy chance-rooted game both solve paths share. */
static void wire_toy_game(pe_external_game_t *game)
{
    game->root = (const void *)(uintptr_t)50u;
    game->player_count = 2u;
    game->is_terminal = terminal;
    game->acting_player = acting;
    game->action_count = actions;
    game->infoset_key = key;
    game->apply_action = apply_action;
    game->action_probability = probability;
    game->terminal_value = utility;
    game->sample_chance_with_user = sample_chance_with_user;
    game->apply_chance = apply_chance;
}

/* ------------------------------------------------------------------ *
 * A wide toy game, for the budget-stop check (issue #249)
 *
 * The narrow game above cannot reach a budget stop: pe_solver_validate()
 * refuses a budget below the estimate, and the estimate's traversal-scratch
 * floor (~12 KiB) is larger than that game's entire real footprint (~1.6 KiB),
 * so no admissible budget is ever crossed. The contract under test needs a run
 * whose storage outgrows the scratch floor, which means many infosets.
 *
 * Shape: a chance root fanning out to BUDGET_FANOUT decision nodes, each with
 * two terminal children. The declaration stays at one infoset, so the estimate
 * stays at the scratch floor while the run builds the real thing.
 * ------------------------------------------------------------------ */
#define BUDGET_FANOUT 1024u

static int wide_terminal(const void *state, void *user)
{
    (void)user;
    if ((uintptr_t)state == 50u) return 0;
    return ((uintptr_t)state % 10u) != 0u;
}

static int wide_acting(const void *state, void *user)
{
    (void)user;
    if ((uintptr_t)state == 50u) return -1;
    return ((uintptr_t)state % 10u) == 0u ? 0 : -1;
}

static uint16_t wide_actions(const void *state, void *user)
{
    (void)user;
    if ((uintptr_t)state == 50u) return 0u;
    return ((uintptr_t)state % 10u) == 0u ? 2u : 0u;
}

static uint64_t wide_key(const void *state, void *user)
{
    (void)user;
    return (uint64_t)(uintptr_t)state;
}

static const void *wide_apply_action(const void *state, uint16_t action,
                                     void *user)
{
    (void)user;
    if ((uintptr_t)state == 50u || ((uintptr_t)state % 10u) != 0u)
        return NULL;
    return (const void *)(uintptr_t)((uintptr_t)state + 1u + action);
}

static double wide_probability(const void *state, uint64_t infoset,
                               uint16_t action, void *user)
{
    (void)state; (void)infoset; (void)action; (void)user;
    return 0.5;
}

static double wide_utility(const void *state, int player, void *user)
{
    (void)user;
    if (player != 0) return 0.0;
    return ((uintptr_t)state % 3u) == 0u ? 1.0 : -1.0;
}

static int wide_sample_chance(const void *state, pe_rng_t *rng,
                              pe_chance_sample_t *out, void *user)
{
    (void)user;
    if ((uintptr_t)state != 50u || !rng || !out) return -1;
    out->outcome = (int)pe_rng_below(rng, BUDGET_FANOUT);
    out->importance_ratio = 1.0;
    return 0;
}

static const void *wide_apply_chance(const void *state, int outcome, void *user)
{
    (void)user;
    if ((uintptr_t)state != 50u || outcome < 0 ||
        (uint32_t)outcome >= BUDGET_FANOUT)
        return NULL;
    return (const void *)(uintptr_t)(1000u + (uintptr_t)outcome * 10u);
}

static void wire_wide_game(pe_external_game_t *game)
{
    game->root = (const void *)(uintptr_t)50u;
    game->player_count = 2u;
    game->is_terminal = wide_terminal;
    game->acting_player = wide_acting;
    game->action_count = wide_actions;
    game->infoset_key = wide_key;
    game->apply_action = wide_apply_action;
    game->action_probability = wide_probability;
    game->terminal_value = wide_utility;
    game->sample_chance_with_user = wide_sample_chance;
    game->apply_chance = wide_apply_chance;
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

    wire_toy_game(&game);

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

/* Issue #249: a run stopped by the memory budget before its first
 * best-response measurement still reports the storage it actually built and
 * the tier that actually ran. Before the fix the memory accounting came back
 * zeroed -- and, worse, every tier was attributed to FULL, a plausible-looking
 * wrong value rather than an obviously absent one. The convergence block stays
 * unmeasured and the status still says so; only the fields that never needed a
 * measurement are now filled. */
static int check_budget_stop_metrics(pe_storage_policy_t tier)
{
    pe_external_game_t game = {0};
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_progress_t progress;
    /* Zeroed, not left uninitialised: the point of the checks below is that
       the call *writes* these fields, so a failure has to be deterministic
       rather than dependent on whatever was on the stack. */
    pe_metrics_t metrics = {0};
    pe_estimate_t estimate;
    pe_solver_status_t status;
    int failed = 0;

    wire_wide_game(&game);

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.max_iterations = 2000u;
    /* Declare one infoset against a game that has BUDGET_FANOUT of them: the
       estimate follows the declaration while the run follows reality, which is
       what leaves a budget that passes validation and is still crossed. */
    cfg.problem.expected_infosets = 1u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.execution.sample_batch_size = 16u;
    cfg.execution.precision = PE_PREC_F32;
    cfg.execution.storage_policy = tier;
    cfg.seed = 0x1234u;
    cfg.target_exploitability_mbb = 0.0;
    /* The only in-loop measurement is at iteration 1000, so a budget stop
       anywhere before that leaves the convergence block unmeasured. */
    cfg.exploitability_interval = 1000u;
    deps.external_game = &game;

    /* Validation refuses a plan whose estimate does not fit the budget, so a
       budget that merely "looks small" never reaches the loop. Read what this
       shape really needs first, then set the budget to it: it clears the
       estimate by construction, and the run crosses it once the storage it
       actually builds outgrows the declared shape. */
    cfg.execution.max_ram_bytes = 0u;
    solver = pe_solver_create(&cfg, &deps);
    if (!solver || pe_solver_estimate(solver, &estimate) != PE_SOLVER_OK)
    {
        fprintf(stderr, "budget-stop estimate failed (tier %d)\n", (int)tier);
        pe_solver_destroy(solver);
        return -1;
    }
    pe_solver_destroy(solver);

    cfg.execution.max_ram_bytes = estimate.host_bytes;
    solver = pe_solver_create(&cfg, &deps);
    if (!solver || pe_solver_run(solver) != PE_SOLVER_OK)
    {
        fprintf(stderr, "budget-stop solve failed (tier %d)\n", (int)tier);
        pe_solver_destroy(solver);
        return -1;
    }
    if (pe_solver_progress(solver, &progress) != PE_SOLVER_OK ||
        progress.complete || progress.stop_cause != PE_STOP_MEMORY_BUDGET)
    {
        fprintf(stderr, "tier %d did not stop on the memory budget "
                "(cause %d, iter %llu, mem %llu)\n", (int)tier,
                (int)progress.stop_cause,
                (unsigned long long)progress.iteration,
                (unsigned long long)progress.memory_bytes);
        failed = 1;
    }
    /* The status keeps its meaning: the convergence block was never
       measured. */
    status = pe_solver_metrics(solver, &metrics);
    if (status != PE_SOLVER_ERR_INVALID_STATE)
    {
        fprintf(stderr, "tier %d unmeasured metrics returned status %d\n",
                (int)tier, (int)status);
        failed = 1;
    }
    /* Everything that does not depend on a measurement is still filled. */
    if (metrics.storage_memory.total_infosets == 0u ||
        metrics.storage_memory.storage_bytes == 0u ||
        metrics.storage_memory.bytes_per_infoset <= 0.0)
    {
        fprintf(stderr,
                "tier %d reported zeroed storage accounting "
                "(infosets %zu, bytes %llu, per-infoset %g)\n",
                (int)tier, metrics.storage_memory.total_infosets,
                (unsigned long long)metrics.storage_memory.storage_bytes,
                metrics.storage_memory.bytes_per_infoset);
        failed = 1;
    }
    if (metrics.storage_memory_policy != tier)
    {
        fprintf(stderr, "tier %d was reported as policy %d\n", (int)tier,
                (int)metrics.storage_memory_policy);
        failed = 1;
    }
    /* And the convergence block is absence, not a measured zero. */
    if (metrics.guarantee != PE_GUARANTEE_UNSPECIFIED ||
        metrics.sample_count != 0u || metrics.measurement_iteration != 0u)
    {
        fprintf(stderr, "tier %d invented a convergence measurement\n",
                (int)tier);
        failed = 1;
    }
    pe_solver_destroy(solver);
    return failed ? -1 : 0;
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

    /* Case-insensitive parse, like the other solver enum parsers: the CLI
     * accepts the case variants that precision and backend accept. */
    if (pe_storage_policy_from_name("FULL") != PE_STORAGE_FULL ||
        pe_storage_policy_from_name("Recompute-Deep") !=
            PE_STORAGE_RECOMPUTE_DEEP ||
        pe_storage_policy_from_name("COMPACT") != PE_STORAGE_COMPACT ||
        pe_storage_policy_from_name("full") != PE_STORAGE_FULL ||
        pe_storage_policy_from_name("nope") != PE_STORAGE_POLICY_COUNT ||
        pe_storage_policy_from_name("") != PE_STORAGE_POLICY_COUNT ||
        pe_storage_policy_from_name(NULL) != PE_STORAGE_POLICY_COUNT)
    {
        fprintf(stderr, "pe_storage_policy_from_name is not case-insensitive\n");
        return 1;
    }

    /* MIXED is not a staged compact precision: it stages no compact arrays,
     * so a tier must drop nothing and invent no accounting. (It cannot
     * allocate dense arrays either -- a pre-existing storage gap tracked
     * separately; the tier contract only needs "nothing recomputable".) */
    {
        pe_storage_t *mixeds = pe_storage_create_with_tier(
            8u, PE_PREC_MIXED, PE_STORAGE_RECOMPUTE_DEEP);
        pe_storage_memory_report_t mixedr;

        if (!mixeds)
        {
            fprintf(stderr, "tiered MIXED creation failed\n");
            return 1;
        }
        if (pe_storage_resolve(mixeds, 3u, 2, 1, 0) == PE_INFOSET_ID_INVALID)
        {
            fprintf(stderr, "MIXED resolve failed\n");
            return 1;
        }
        pe_storage_memory_report(mixeds, &mixedr);
        if (mixedr.staging_bytes != 0)
        {
            fprintf(stderr, "MIXED storage staged recomputable state (%zu)\n",
                    mixedr.staging_bytes);
            return 1;
        }
        if (pe_storage_drop_recomputable(mixeds, 0) != 0)
        {
            fprintf(stderr, "MIXED drop failed\n");
            return 1;
        }
        pe_storage_memory_report(mixeds, &mixedr);
        if (mixedr.evict_calls != 0 || mixedr.remat_calls != 0)
        {
            fprintf(stderr, "MIXED storage invented recomputable state\n");
            return 1;
        }
        pe_storage_destroy(mixeds);
    }

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

    /* Issue #249: a budget stop still reports the storage it built and the
     * tier that ran, for every tier. */
    for (int t = 0; t < 3; ++t)
        if (check_budget_stop_metrics(tiers[t]) != 0)
            return 1;

    puts("test_storage_tiers_solve: tiers preserve strategy results");
    return 0;
}
