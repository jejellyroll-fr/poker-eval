/*
 * test_cfr_dcfr_discount.c - EXT-07: the DCFR discount applies once per iteration
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * Discounted CFR scales the cumulative regret once per iteration, applied to
 * what was there before that iteration's deltas:
 *
 *     R_t = R_(t-1) * d(t) + r_t
 *
 * The v2 solver passed d to every per-node regret update instead. A poker
 * infoset is reached from many states in a single iteration, so it was scaled
 * d^N — a discount exponentially stronger than the algorithm calls for, and
 * one that varied with the shape of the tree rather than with the iteration.
 *
 * Measuring that exactly needs the deltas out of the way, so the game below
 * gives every terminal the same utility: every action has the same value, so
 * every regret delta is exactly zero and whatever remains in the storage is
 * the discount and nothing else. An infoset reached four times per iteration
 * must come out scaled by d, not by d^4.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
            g_failures++;                                      \
        }                                                      \
    } while (0)

/* state_key: 0 = root (player 0, 4 actions); 1..4 = the four children
   (player 1, 2 actions); 100+ = terminal. */
#define ROOT      0
#define SHARED_KEY 0xABCDEF01ull

static int t_player(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)u; return (k == ROOT) ? 0 : 1; }

static int t_terminal(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)u; return k >= 100; }

/* Every terminal pays the same, so every action is worth the same and every
   regret delta is exactly zero. */
static double t_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{ (void)g; (void)k; (void)p; (void)u; return 0.0; }

static int t_actions(cfr_game_t *g, uint64_t k, int *out, int max, void *u)
{
    (void)g; (void)u;
    int n = (k == ROOT) ? 4 : 2;
    if (n > max) n = max;
    for (int i = 0; i < n; ++i) out[i] = i;
    return n;
}

static uint64_t t_apply(cfr_game_t *g, uint64_t k, int a, void *u)
{ (void)g; (void)u; return (k == ROOT) ? (uint64_t)(1 + a) : (uint64_t)(100 + a); }

/* The four children all belong to one infoset: that is what makes it visited
   four times in a single traversal. */
static uint64_t t_infoset_key(const void *state)
{
    uint64_t k = (uint64_t)(uintptr_t)state;
    if (k == ROOT) return ROOT;
    if (k >= 1 && k <= 4) return SHARED_KEY;
    return k;
}

static void build(cfr_game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->current_player = t_player;
    g->get_actions = t_actions;
    g->apply_action = t_apply;
    g->is_terminal = t_terminal;
    g->get_utility = t_utility;
    g->get_infoset_key = t_infoset_key;
    g->num_players = 2;
    g->initial_state = (void *)(uintptr_t)ROOT;
}

/*
 * Recover the absolute value of a regret entry through the public API.
 *
 * cfr_storage_get_regret_strategy_at_street normalises, so a ratio alone
 * cannot tell R * d from R * d^4 — a scale-invariant reading is exactly how a
 * first version of this test passed against the very defect it was written to
 * catch. Adding a known probe to the second action breaks the invariance:
 *
 *     s0 = x / (x + probe)   =>   x = probe * s0 / (1 - s0)
 *
 * The probe is destructive, so this is called once per storage, last.
 */
static double measure_regret(cfr_storage_t *s, uint64_t key, int n, double probe)
{
    double delta[8] = {0};
    double strat[8];

    delta[1] = probe;
    cfr_storage_update_regret(s, key, n, delta, 1.0);
    cfr_storage_get_regret_strategy_at_street(s, key, n, -1, strat);

    if (strat[0] >= 1.0)
        return -1.0;                 /* probe swamped: caller will notice */
    return probe * strat[0] / (1.0 - strat[0]);
}

/*
 * The scaling primitive on its own: this is what has to happen once, and the
 * integration check above only makes sense if it is exact.
 */
static void test_scale_regrets_is_exact(void)
{
    cfr_storage_t *s = cfr_storage_create();
    double seeded[3] = { 4.0, -2.0, 1.0 };
    double probs[3];

    cfr_storage_update_regret(s, 42, 3, seeded, 1.0);
    cfr_storage_scale_regrets(s, 0.5);

    /* 2.0 / (2.0 + 0.5) for the two positive entries. */
    cfr_storage_get_regret_strategy_at_street(s, 42, 3, -1, probs);
    CHECK(fabs(probs[0] - 0.8) < 1e-12, "scaled regret ratio is %.17g", probs[0]);
    CHECK(fabs(probs[2] - 0.2) < 1e-12, "scaled regret ratio is %.17g", probs[2]);

    /* A neutral factor must not touch anything, and must not cost a sweep. */
    cfr_storage_scale_regrets(s, 1.0);
    cfr_storage_get_regret_strategy_at_street(s, 42, 3, -1, probs);
    CHECK(fabs(probs[0] - 0.8) < 1e-12, "a factor of 1.0 changed the regrets");

    cfr_storage_scale_regrets(NULL, 0.5); /* must not crash */
    cfr_storage_destroy(s);
}

/*
 * The property the ticket exists for, measured absolutely.
 *
 * With every delta zero the seeded regret is only ever scaled, so what remains
 * is the seed times the discount raised to the number of applications. The
 * infoset below is reached four times in one traversal: d and d^4 differ by a
 * factor of eight, which no tolerance can hide.
 */
static void test_discount_applies_once_per_iteration(void)
{
    const double alpha = 1.5;
    const double d = 1.0 / (1.0 + 1.0);        /* t = 1: 1^a / (1^a + 1) */
    const double seed = 8.0;

    cfr_game_t game;
    cfr_config_t cfg;
    cfr_storage_t *s = cfr_storage_create();
    double seeded[2] = { seed, 0.0 };
    double expl = 0.0;
    double measured;

    build(&game);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 1;
    cfg.max_depth = 32;
    cfg.enable_dcfr = 1;
    cfg.dcfr_alpha = alpha;

    cfr_storage_update_regret(s, SHARED_KEY, 2, seeded, 1.0);
    (void)cfr_solve(&game, s, &cfg, &expl);

    measured = measure_regret(s, SHARED_KEY, 2, 1.0);

    CHECK(fabs(measured - seed * d) < 1e-9,
          "expected the seed discounted once (%.17g), measured %.17g "
          "(four applications would give %.17g)",
          seed * d, measured, seed * d * d * d * d);

    cfr_storage_destroy(s);
}

/*
 * The same discount whatever the tree looks like.
 *
 * The root is reached once per iteration and the shared infoset four times.
 * Under the corrected rule both come out scaled by the same factor, because
 * the discount belongs to the iteration and not to the shape of the tree.
 */
static void test_visit_count_does_not_change_the_discount(void)
{
    cfr_game_t game;
    cfr_config_t cfg;
    cfr_storage_t *s = cfr_storage_create();
    double seed_root[4] = { 8.0, 0.0, 0.0, 0.0 };
    double seed_shared[2] = { 8.0, 0.0 };
    double expl = 0.0;
    double root_regret;
    double shared_regret;

    build(&game);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 1;
    cfg.max_depth = 32;
    cfg.enable_dcfr = 1;
    cfg.dcfr_alpha = 1.5;

    cfr_storage_update_regret(s, ROOT, 4, seed_root, 1.0);
    cfr_storage_update_regret(s, SHARED_KEY, 2, seed_shared, 1.0);
    (void)cfr_solve(&game, s, &cfg, &expl);

    root_regret = measure_regret(s, ROOT, 4, 1.0);
    shared_regret = measure_regret(s, SHARED_KEY, 2, 1.0);

    CHECK(fabs(root_regret - shared_regret) < 1e-9,
          "one visit gave %.17g and four visits gave %.17g: the discount is "
          "following the tree instead of the iteration",
          root_regret, shared_regret);
    CHECK(fabs(root_regret - 4.0) < 1e-9,
          "expected 8.0 discounted once, measured %.17g", root_regret);

    cfr_storage_destroy(s);
}

int main(void)
{
    test_scale_regrets_is_exact();
    test_discount_applies_once_per_iteration();
    test_visit_count_does_not_change_the_discount();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_cfr_dcfr_discount: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_cfr_dcfr_discount: the discount applies once per iteration\n");
    return 0;
}
