/*
 * test_cfr_relock_ev_loss.c - EXT-08: the relock EV-loss measurement
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * FEAT-11 reports what a lock costs: the reach-weighted gap between what the
 * locked player could get by best-responding and what the forced frequencies
 * give it. EXT-08 moved that measurement out of the traversal, and the three
 * existing lock tests turned out not to pin it — removing the reach weighting
 * left all of them green. This closes that.
 *
 * The game is built so every quantity is known in closed form. Both decision
 * nodes are locked, so the descent probabilities are the targets rather than
 * anything learned, and every terminal pays a constant:
 *
 *   root (infoset R, locked 0.25 / 0.75)
 *     |-- a0 --> S1 (infoset K, locked 0.25 / 0.75) --> pays 4.0 / 0.0
 *     `-- a1 --> S2 (infoset K, same lock)          --> pays 1.0 / 0.0
 *
 * K is therefore visited twice per traversal, with the acting player's reach
 * equal to 0.25 at S1 and 0.75 at S2. At each visit:
 *
 *   S1: br = max(4, 0) = 4    forced = 0.25*4 + 0.75*0 = 1     loss = 3
 *   S2: br = max(1, 0) = 1    forced = 0.25*1 + 0.75*0 = 0.25  loss = 0.75
 *
 * Aggregated over the infoset: br = 1.75, forced = 0.4375, loss = 1.3125.
 *
 * Both locks are asymmetric on purpose. With 0.5 / 0.5 on K, weighting the
 * forced value by the lock would be indistinguishable from averaging the
 * branches, and a mutation that replaced one with the other went unnoticed.
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

#define ROOT 0u
#define S1   1u
#define S2   2u
#define KEY_ROOT 0x1000ull
#define KEY_K    0x2000ull

/* Terminals are 10 + 2*branch + action, so their utility is a lookup. */
static double terminal_payoff(uint64_t k)
{
    switch (k)
    {
    case 10: return 4.0;   /* S1, action 0 */
    case 11: return 0.0;   /* S1, action 1 */
    case 12: return 1.0;   /* S2, action 0 */
    case 13: return 0.0;   /* S2, action 1 */
    default: return 0.0;
    }
}

static int t_player(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)k; (void)u; return 0; }

static int t_terminal(cfr_game_t *g, uint64_t k, void *u)
{ (void)g; (void)u; return k >= 10; }

static double t_utility(cfr_game_t *g, uint64_t k, int p, void *u)
{ (void)g; (void)u; double v = terminal_payoff(k); return (p == 0) ? v : -v; }

static int t_actions(cfr_game_t *g, uint64_t k, int *out, int max, void *u)
{
    (void)g; (void)k; (void)u;
    int n = (max < 2) ? max : 2;
    for (int i = 0; i < n; ++i) out[i] = i;
    return n;
}

static uint64_t t_apply(cfr_game_t *g, uint64_t k, int a, void *u)
{
    (void)g; (void)u;
    if (k == ROOT) return (uint64_t)(1 + a);          /* S1 / S2 */
    if (k == S1)   return (uint64_t)(10 + a);
    return (uint64_t)(12 + a);                         /* S2 */
}

/* S1 and S2 are the same information set: that is what makes the aggregation
   across states — and therefore the reach weighting — observable. */
static uint64_t t_infoset_key(const void *state)
{
    uint64_t k = (uint64_t)(uintptr_t)state;
    if (k == ROOT) return KEY_ROOT;
    if (k == S1 || k == S2) return KEY_K;
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

static void test_relock_ev_loss_is_reach_weighted(void)
{
    cfr_game_t game;
    cfr_config_t cfg;
    cfr_storage_t *s = cfr_storage_create();
    double root_lock[2] = { 0.25, 0.75 };
    double k_lock[2] = { 0.25, 0.75 };
    double loss = 0.0, br = 0.0, forced = 0.0;
    double expl = 0.0;

    build(&game);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 1;
    cfg.max_depth = 32;
    /* lock_period 1 makes every iteration a relock iteration. */
    cfg.enable_periodic_relock = 1;
    cfg.lock_period = 1;

    CHECK(cfr_storage_set_locked_strategy(s, KEY_ROOT, root_lock, 2) == 0,
          "root lock rejected");
    CHECK(cfr_storage_set_locked_strategy(s, KEY_K, k_lock, 2) == 0,
          "shared-infoset lock rejected");

    (void)cfr_solve(&game, s, &cfg, &expl);

    CHECK(cfr_storage_get_lock_ev_loss(s, KEY_K, &loss, &br, &forced) == 1,
          "no EV loss recorded for the locked infoset");

    /* 0.25 * 4 + 0.75 * 1 = 1.75 */
    CHECK(fabs(br - 1.75) < 1e-9,
          "reach-weighted best-response value is %.17g, expected 1.75", br);
    /* 0.25 * 1 + 0.75 * 0.25 = 0.4375; averaging the branches instead of
       weighting them by the lock would give 0.875. */
    CHECK(fabs(forced - 0.4375) < 1e-9,
          "reach-weighted forced value is %.17g, expected 0.4375", forced);
    /* br - forced, aggregated the same way. Dropping the reach weighting
       gives 1.875 instead. */
    CHECK(fabs(loss - 1.3125) < 1e-9,
          "reach-weighted EV loss is %.17g, expected 1.3125", loss);

    cfr_storage_destroy(s);
}

static void test_frozen_node_plays_its_target(void)
{
    cfr_game_t game;
    cfr_config_t cfg;
    cfr_storage_t *s = cfr_storage_create();
    double k_lock[2] = { 0.25, 0.75 };
    double avg[2];
    double expl = 0.0;

    build(&game);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 20;
    cfg.max_depth = 32;
    /* Freeze mode: no periodic relock at all. */

    CHECK(cfr_storage_set_locked_strategy(s, KEY_K, k_lock, 2) == 0, "lock rejected");
    (void)cfr_solve(&game, s, &cfg, &expl);

    /* A frozen infoset exports exactly its target however much the solve
       would otherwise have moved it. */
    cfr_storage_get_avg_strategy(s, KEY_K, 2, avg);
    CHECK(fabs(avg[0] - 0.25) < 1e-9 && fabs(avg[1] - 0.75) < 1e-9,
          "a frozen infoset exported %.17g / %.17g instead of its target",
          avg[0], avg[1]);

    cfr_storage_destroy(s);
}

int main(void)
{
    test_relock_ev_loss_is_reach_weighted();
    test_frozen_node_plays_its_target();

    if (g_failures != 0)
    {
        fprintf(stderr, "test_cfr_relock_ev_loss: %d failure(s)\n", g_failures);
        return 1;
    }

    printf("test_cfr_relock_ev_loss: the relock EV loss is reach-weighted\n");
    return 0;
}
