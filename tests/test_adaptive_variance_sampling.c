/*
 * test_adaptive_variance_sampling.c - PE_SAMPLING_ADAPTIVE_VARIANCE in the
 * external-sampling traversal and the solver (issue #256).
 *
 * A toy game with a known answer:
 *
 *   root chance (4 deals, depth 0)
 *     -> player 0, one infoset, 2 actions
 *          -> chance (8 outcomes, depth 1)
 *               -> terminal: base[action] + deal_effect[deal] + noise * (m - 3.5)
 *
 * The regret player 0 receives for action 0 at its infoset has the exact
 * expectation 0.5 * (base[0] - base[1]) per iteration, whatever the sampling
 * policy, because each chance value is an unbiased estimate and the
 * adaptive policy weights its R replicates 1/R each.
 *
 * Checks:
 *  1. The pilot: until check_interval visits of a group, it draws the
 *     minimum.
 *  2. Zero variance keeps every visit at the minimum; a tiny tolerance drives
 *     the budget to the maximum.
 *  3. Nested chance visits never multiply one trajectory past max_samples.
 *  4. The update mass per iteration is exactly the standard one: the root
 *     infoset's average-strategy increments sum to 1.
 *  5. No bias: the mean regret matches its exact expectation within four
 *     standard errors, for standard, fixed R and adaptive sampling.
 *  6. Determinism: the same seed gives the same batches.
 *  7. Grouping: street-tagged draws use the street groups, untagged ones the
 *     chance-depth groups; invalid settings are refused.
 *  8. Solver level: fixed and adaptive sampling under pe_solver reach
 *     compatible strategies, and adaptive spends fewer terminal evaluations
 *     where the game has no variance to resolve.
 */

#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

/* ---------------------------------------------------------------- *
 * The toy game
 * ---------------------------------------------------------------- */

#define DEALS 4
#define OUTCOMES 8
#define ROOT 1u
#define PLAYER_BASE 10u     /* 10 + deal */
#define CHANCE_BASE 20u     /* 20 + deal * 2 + action */
#define TERMINAL_BASE 1000u /* 1000 + chance * 16 + outcome */

static double g_base[2] = {1.0, 0.0};
static double g_deal_effect[DEALS] = {0.0, 0.0, 0.0, 0.0};
static double g_noise = 0.0;
static int8_t g_root_street = -1; /* PE_STREET_UNKNOWN */
static int8_t g_leaf_street = -1;
static uint64_t g_terminal_evals = 0;

static int is_terminal(const void *s, void *u)
{
    (void)u;
    return (uintptr_t)s >= TERMINAL_BASE;
}

static int acting_player(const void *s, void *u)
{
    uintptr_t v = (uintptr_t)s;
    (void)u;
    return (v >= PLAYER_BASE && v < PLAYER_BASE + DEALS) ? 0 : -1;
}

static uint16_t action_count(const void *s, void *u)
{
    return acting_player(s, u) == 0 ? 2u : 0u;
}

/* Player 0 does not see the deal: one infoset. */
static uint64_t infoset_key(const void *s, void *u)
{
    (void)s;
    (void)u;
    return 77u;
}

static const void *apply_action(const void *s, uint16_t a, void *u)
{
    uintptr_t v = (uintptr_t)s;
    (void)u;
    if (v < PLAYER_BASE || v >= PLAYER_BASE + DEALS || a > 1u)
        return NULL;
    return (const void *)(CHANCE_BASE + (v - PLAYER_BASE) * 2u + a);
}

static double action_probability(const void *s, uint64_t k, uint16_t a, void *u)
{
    (void)s;
    (void)k;
    (void)a;
    (void)u;
    return 0.5;
}

static double terminal_value(const void *s, int player, void *u)
{
    uintptr_t v = (uintptr_t)s - TERMINAL_BASE;
    uintptr_t chance = v / 16u, outcome = v % 16u;
    uintptr_t deal = (chance - CHANCE_BASE) / 2u, action = (chance - CHANCE_BASE) % 2u;
    double value = g_base[action] + g_deal_effect[deal] +
                   g_noise * ((double)outcome - 3.5);
    (void)u;
    g_terminal_evals++;
    return player == 0 ? value : -value;
}

static int sample_chance(const void *s, pe_rng_t *rng, pe_chance_sample_t *out)
{
    uintptr_t v = (uintptr_t)s;
    if (v == ROOT)
    {
        out->outcome = (int)pe_rng_below(rng, DEALS);
        out->importance_ratio = 1.0;
        out->street = g_root_street;
        return 0;
    }
    if (v >= CHANCE_BASE && v < CHANCE_BASE + 2u * DEALS)
    {
        out->outcome = (int)pe_rng_below(rng, OUTCOMES);
        out->importance_ratio = 1.0;
        out->street = g_leaf_street;
        return 0;
    }
    return -1;
}

static const void *apply_chance(const void *s, int outcome, void *u)
{
    uintptr_t v = (uintptr_t)s;
    (void)u;
    if (v == ROOT && outcome >= 0 && outcome < DEALS)
        return (const void *)(PLAYER_BASE + (uintptr_t)outcome);
    if (v >= CHANCE_BASE && v < CHANCE_BASE + 2u * DEALS && outcome >= 0 &&
        outcome < OUTCOMES)
        return (const void *)(TERMINAL_BASE + v * 16u + (uintptr_t)outcome);
    return NULL;
}

static pe_external_game_t make_game(void)
{
    pe_external_game_t g;
    memset(&g, 0, sizeof(g));
    g.root = (const void *)ROOT;
    g.player_count = 2u;
    g.is_terminal = is_terminal;
    g.acting_player = acting_player;
    g.action_count = action_count;
    g.infoset_key = infoset_key;
    g.apply_action = apply_action;
    g.action_probability = action_probability;
    g.terminal_value = terminal_value;
    g.sample_chance = sample_chance;
    g.apply_chance = apply_chance;
    return g;
}

static void set_game(double noise, double deal_spread)
{
    g_noise = noise;
    for (int d = 0; d < DEALS; ++d)
        g_deal_effect[d] = deal_spread * ((double)d - 1.5);
    g_root_street = -1;
    g_leaf_street = -1;
}

/* ---------------------------------------------------------------- *
 * Running the traversal directly
 * ---------------------------------------------------------------- */

typedef struct
{
    pe_external_sampling_ctx_t ctx;
    pe_update_batch_t batch;
    pe_storage_t *storage;
    pe_external_game_t game;
} runner_t;

static int runner_open(runner_t *r, pe_sampling_policy_t policy,
                       const pe_adaptive_sampling_t *adaptive, uint64_t seed)
{
    memset(r, 0, sizeof(*r));
    r->game = make_game();
    r->storage = pe_storage_create(8u);
    if (!r->storage ||
        pe_external_sampling_ctx_init(&r->ctx, &r->game, pe_storage_ram_ops(),
                                      r->storage, 0, seed) != 0)
        return -1;
    pe_external_sampling_set_policy(&r->ctx, policy, NULL);
    if (policy == PE_SAMPLING_ADAPTIVE_VARIANCE && adaptive &&
        pe_external_sampling_set_adaptive(&r->ctx, adaptive) != 0)
        return -1;
    return 0;
}

static void runner_close(runner_t *r)
{
    pe_update_batch_destroy(&r->batch);
    pe_external_sampling_ctx_destroy(&r->ctx);
    pe_storage_destroy(r->storage);
}

/* One iteration; returns the regret delta for action 0 of the one infoset
   and its average-strategy mass. */
static int runner_step(runner_t *r, double *regret0, double *avg_mass)
{
    double reg = 0.0, mass = 0.0;
    if (pe_external_sampling_run(&r->ctx, &r->batch) != 0)
        return -1;
    for (size_t u = 0; u < r->batch.count; ++u)
    {
        if (r->batch.items[u].action == 0u)
            reg += r->batch.items[u].delta;
        mass += r->batch.items[u].average_delta;
    }
    if (regret0)
        *regret0 = reg;
    if (avg_mass)
        *avg_mass = mass;
    return 0;
}

static pe_adaptive_sampling_t settings(uint32_t min, uint32_t max,
                                       uint32_t interval, double abs_tol)
{
    pe_adaptive_sampling_t s;
    memset(&s, 0, sizeof(s));
    s.min_samples = min;
    s.max_samples = max;
    s.check_interval = interval;
    s.absolute_tolerance = abs_tol;
    return s;
}

#define GROUP_DEPTH0 4
#define GROUP_DEPTH1 5

static void test_pilot_and_budgets(void)
{
    runner_t r;
    pe_adaptive_sampling_t s;

    printf("  pilot, budgets and the trajectory cap\n");

    /* 1. Pilot: the first 50 root visits draw the minimum, 2 each. */
    set_game(1.0, 1.0);
    s = settings(2u, 16u, 50u, 1e-6);
    CHECK(runner_open(&r, PE_SAMPLING_ADAPTIVE_VARIANCE, &s, 11u) == 0, "open");
    for (int i = 0; i < 50; ++i)
        runner_step(&r, NULL, NULL);
    CHECK(r.ctx.adaptive_stats[GROUP_DEPTH0].estimates == 50u &&
              r.ctx.adaptive_stats[GROUP_DEPTH0].samples == 100u,
          "pilot: 50 root visits at 2 draws (got %llu visits, %llu draws)",
          (unsigned long long)r.ctx.adaptive_stats[GROUP_DEPTH0].estimates,
          (unsigned long long)r.ctx.adaptive_stats[GROUP_DEPTH0].samples);
    /* 2b. Tolerance 1e-6 with real spread: the next budget is the maximum. */
    CHECK(r.ctx.adaptive_budget[0][GROUP_DEPTH0] == 16u,
          "a tiny tolerance drives the budget to the maximum (got %u)",
          r.ctx.adaptive_budget[0][GROUP_DEPTH0]);

    /* 3. The trajectory cap: from here on the root draws 16, so each deal
       has 16 / 16 = 1 draw left for its leaf chance. Two actions per deal:
       at most 2 * 16 terminals per iteration, and never more, pilot
       included. */
    {
        int within = 1;
        for (int i = 0; i < 200; ++i)
        {
            runner_step(&r, NULL, NULL);
            if (r.ctx.terminal_nodes > 2u * 16u)
                within = 0;
        }
        CHECK(within, "a trajectory never multiplies past max_samples");
        CHECK(r.ctx.adaptive_stats[GROUP_DEPTH1].max_hits > 0u,
              "the leaf visits report hitting the cap");
    }
    runner_close(&r);

    /* 2a. Zero variance: every visit stays at the minimum. */
    set_game(0.0, 0.0);
    s = settings(2u, 16u, 20u, 1e-6);
    CHECK(runner_open(&r, PE_SAMPLING_ADAPTIVE_VARIANCE, &s, 12u) == 0, "open");
    for (int i = 0; i < 300; ++i)
        runner_step(&r, NULL, NULL);
    for (int g = GROUP_DEPTH0; g <= GROUP_DEPTH1; ++g)
    {
        const pe_adaptive_group_stats_t *st = &r.ctx.adaptive_stats[g];
        CHECK(st->estimates > 0u && st->samples == 2u * st->estimates &&
                  st->min_hits == st->estimates && st->max_hits == 0u &&
                  st->resolved == st->estimates,
              "zero variance, group %d: %llu visits, %llu draws, all resolved",
              g, (unsigned long long)st->estimates,
              (unsigned long long)st->samples);
    }
    runner_close(&r);
}

static void test_weighting_and_bias(void)
{
    const pe_sampling_policy_t policies[3] = {PE_SAMPLING_STANDARD,
                                              PE_SAMPLING_ADAPTIVE_VARIANCE,
                                              PE_SAMPLING_ADAPTIVE_VARIANCE};
    const char *names[3] = {"standard", "fixed R=4", "adaptive"};
    pe_adaptive_sampling_t fixed = settings(4u, 4u, 16u, 0.0);
    pe_adaptive_sampling_t adaptive = settings(2u, 16u, 16u, 0.5);
    const double expected = 0.5 * (g_base[0] - g_base[1]);

    printf("  update weighting and unbiasedness\n");
    set_game(3.0, 2.0);
    for (int k = 0; k < 3; ++k)
    {
        runner_t r;
        double sum = 0.0, sum_sq = 0.0;
        int mass_exact = 1;
        const int iterations = 20000;
        CHECK(runner_open(&r, policies[k], k == 1 ? &fixed : &adaptive,
                          0xB1A5u + (uint64_t)k) == 0,
              "open %s", names[k]);
        for (int i = 0; i < iterations; ++i)
        {
            double reg = 0.0, mass = 0.0;
            if (runner_step(&r, &reg, &mass) != 0)
            {
                CHECK(0, "%s: traversal failed", names[k]);
                break;
            }
            /* 4. The average-strategy mass is the standard one, 1. */
            if (fabs(mass - 1.0) > 1e-12)
                mass_exact = 0;
            sum += reg;
            sum_sq += reg * reg;
        }
        double mean = sum / iterations;
        double var = (sum_sq - sum * sum / iterations) / (iterations - 1);
        double se = sqrt(var / iterations);
        CHECK(mass_exact, "%s: each iteration carries exactly one unit of "
              "average-strategy mass", names[k]);
        /* 5. Unbiased: the mean regret is 0.5 * (1 - 0) = 0.5. */
        CHECK(fabs(mean - expected) <= 4.0 * se,
              "%s: mean regret %.5f, expected %.5f (4 SE = %.5f)", names[k],
              mean, expected, 4.0 * se);
        printf("    %-10s mean regret %.4f +- %.4f (exact %.4f), %llu "
               "terminal evaluations\n",
               names[k], mean, se, expected,
               (unsigned long long)r.ctx.total_terminal_nodes);
        runner_close(&r);
    }
}

static void test_determinism_and_groups(void)
{
    pe_adaptive_sampling_t s = settings(2u, 8u, 10u, 0.1);
    runner_t a, b;
    int same = 1;

    printf("  determinism, grouping and settings\n");
    set_game(2.0, 1.0);
    runner_open(&a, PE_SAMPLING_ADAPTIVE_VARIANCE, &s, 99u);
    runner_open(&b, PE_SAMPLING_ADAPTIVE_VARIANCE, &s, 99u);
    for (int i = 0; i < 300 && same; ++i)
    {
        double ra, rb, ma, mb;
        runner_step(&a, &ra, &ma);
        runner_step(&b, &rb, &mb);
        if (a.batch.count != b.batch.count ||
            memcmp(&ra, &rb, sizeof(ra)) != 0 ||
            a.ctx.total_terminal_nodes != b.ctx.total_terminal_nodes)
            same = 0;
    }
    CHECK(same, "the same seed reproduces the run");
    runner_close(&a);
    runner_close(&b);

    /* Street-tagged leaves use the street group; the untagged root keeps
       its chance-depth group. */
    set_game(2.0, 1.0);
    g_leaf_street = 3;
    runner_open(&a, PE_SAMPLING_ADAPTIVE_VARIANCE, &s, 5u);
    for (int i = 0; i < 50; ++i)
        runner_step(&a, NULL, NULL);
    CHECK(a.ctx.adaptive_stats[3].estimates > 0u &&
              a.ctx.adaptive_stats[GROUP_DEPTH1].estimates == 0u &&
              a.ctx.adaptive_stats[GROUP_DEPTH0].estimates == 50u,
          "tagged leaves group as street:river, the root as chance-depth:0");
    CHECK(strcmp(pe_adaptive_group_name(3), "street:river") == 0 &&
              strcmp(pe_adaptive_group_name(GROUP_DEPTH1), "chance-depth:1") == 0,
          "group names");

    /* Invalid settings leave the ctx as it was. */
    {
        pe_adaptive_sampling_t bad = settings(8u, 4u, 10u, 0.1);
        uint32_t before = a.ctx.adaptive_budget[0][GROUP_DEPTH0];
        CHECK(pe_external_sampling_set_adaptive(&a.ctx, &bad) != 0 &&
                  a.ctx.adaptive_budget[0][GROUP_DEPTH0] == before,
              "min above max is refused and changes nothing");
    }
    runner_close(&a);
    g_leaf_street = -1;
}

/* ---------------------------------------------------------------- *
 * 8. Solver level
 * ---------------------------------------------------------------- */

static double solve(pe_sampling_policy_t policy,
                    const pe_adaptive_sampling_t *adaptive, uint64_t seed,
                    uint64_t *out_evals)
{
    pe_external_game_t game = make_game();
    pe_solver_config_t cfg = pe_solver_config_default();
    pe_solver_deps_t deps = pe_solver_deps_default();
    pe_solver_t *solver;
    pe_strategy_query_t query;
    pe_strategy_view_t view;
    double p0 = -1.0;

    cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
    cfg.algorithm.sampling_policy = policy;
    if (adaptive)
        cfg.algorithm.adaptive = *adaptive;
    cfg.max_iterations = 600u;
    cfg.problem.expected_infosets = 1u;
    cfg.problem.expected_actions = 2u;
    cfg.problem.expected_combos = 1u;
    cfg.seed = (uint32_t)seed;
    deps.external_game = &game;
    g_terminal_evals = 0;
    solver = pe_solver_create(&cfg, &deps);
    if (!solver || pe_solver_run(solver) != PE_SOLVER_OK)
    {
        pe_solver_destroy(solver);
        return -1.0;
    }
    query.infoset = 0u;
    if (pe_solver_strategy(solver, &query, &view) == PE_SOLVER_OK &&
        view.action_count == 2u && view.count >= 2u)
    {
        double total = view.values[0] + view.values[1];
        if (total > 0.0)
            p0 = view.values[0] / total;
    }
    pe_solver_destroy(solver);
    if (out_evals)
        *out_evals = g_terminal_evals;
    return p0;
}

static void test_solver_level(void)
{
    pe_adaptive_sampling_t fixed = settings(8u, 8u, 32u, 0.0);
    /* Leaf draws spread about 1.15; a tolerance of 2 needs only the minimum
       there too, where the fixed policy keeps paying for its 8. */
    pe_adaptive_sampling_t adaptive = settings(2u, 8u, 32u, 2.0);
    uint64_t fixed_evals = 0, adaptive_evals = 0;

    printf("  solver level: fixed and adaptive sampling on the same seeds\n");
    /* Action 0 is worth 1 more, and the leaf noise is small: both policies
       must settle on it. The deals do not change the regret, so the root has
       no variance to resolve and the adaptive policy keeps it at the
       minimum where the fixed one pays for 8 draws. */
    set_game(0.5, 0.0);
    double p_fixed = solve(PE_SAMPLING_ADAPTIVE_VARIANCE, &fixed, 21u, &fixed_evals);
    double p_adaptive =
        solve(PE_SAMPLING_ADAPTIVE_VARIANCE, &adaptive, 21u, &adaptive_evals);
    double p_standard = solve(PE_SAMPLING_STANDARD, NULL, 21u, NULL);
    CHECK(p_fixed > 0.9 && p_adaptive > 0.9 && p_standard > 0.9,
          "every policy plays the better action (fixed %.3f, adaptive %.3f, "
          "standard %.3f)",
          p_fixed, p_adaptive, p_standard);
    CHECK(fabs(p_fixed - p_adaptive) < 0.05,
          "fixed and adaptive strategies are compatible (%.4f vs %.4f)",
          p_fixed, p_adaptive);
    CHECK((double)adaptive_evals < 0.75 * (double)fixed_evals,
          "adaptive spends clearly fewer terminal evaluations (%llu vs %llu)",
          (unsigned long long)adaptive_evals, (unsigned long long)fixed_evals);
    printf("    P(better action): fixed %.4f (%llu evals), adaptive %.4f "
           "(%llu evals), standard %.4f\n",
           p_fixed, (unsigned long long)fixed_evals, p_adaptive,
           (unsigned long long)adaptive_evals, p_standard);

    /* The solver refuses invalid adaptive settings at validation. */
    {
        pe_external_game_t game = make_game();
        pe_solver_config_t cfg = pe_solver_config_default();
        pe_solver_deps_t deps = pe_solver_deps_default();
        cfg.algorithm.preset = PE_PRESET_EXTERNAL_MCCFR;
        cfg.algorithm.sampling_policy = PE_SAMPLING_ADAPTIVE_VARIANCE;
        cfg.algorithm.adaptive.confidence_level = 1.5;
        cfg.max_iterations = 4u;
        deps.external_game = &game;
        pe_solver_t *solver = pe_solver_create(&cfg, &deps);
        CHECK(!solver || pe_solver_run(solver) != PE_SOLVER_OK,
              "a confidence of 1.5 is refused");
        pe_solver_destroy(solver);
    }
}

int main(void)
{
    printf("test_adaptive_variance_sampling: adaptive-variance sampling\n");
    test_pilot_and_budgets();
    test_weighting_and_bias();
    test_determinism_and_groups();
    test_solver_level();
    if (g_failures)
    {
        fprintf(stderr, "test_adaptive_variance_sampling: %d failure(s)\n",
                g_failures);
        return 1;
    }
    printf("test_adaptive_variance_sampling: all checks passed\n");
    return 0;
}
