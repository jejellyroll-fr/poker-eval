#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ASSERT_TRUE(cond, msg)                         \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "Assertion failed: %s\n",  \
                    msg);                              \
            return 1;                                  \
        }                                              \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                        \
    do                                                              \
    {                                                               \
        if ((a) != (b))                                             \
        {                                                           \
            fprintf(stderr, "Assertion failed: %s (%d != %d)\n",    \
                    msg, (int)(a), (int)(b));                       \
            return 1;                                               \
        }                                                           \
    } while (0)

/*
 * A minimal two-player game that allocates a heap state for every
 * apply_action, mirroring how the river adapters work. release_state is
 * wired to free() and a live-accounting counter. If cfr_core fails to call
 * release_state on any apply_action result, the counter leaks and the test
 * fails at the end.
 */

#define MAX_DEPTH 6

typedef struct {
    int to_act;         /* 0 or 1 */
    int depth;
} leak_state_t;

static long g_live_states = 0;

static int leak_is_term(cfr_game_t *g, uint64_t key, void *u)
{
    (void)g; (void)u;
    return ((leak_state_t *)(uintptr_t)key)->depth >= MAX_DEPTH;
}

static double leak_utility(cfr_game_t *g, uint64_t key, int player, void *u)
{
    (void)g; (void)u;
    /* Deterministic payoff: terminal value is a function of depth so both
       best-response branches are exercised; sign zero-sum. */
    (void)player;
    return 1.0 - (double)player;
}

static int leak_n_actions(cfr_game_t *g, uint64_t key, int *out, int max, void *u)
{
    (void)g; (void)u;
    (void)max;
    out[0] = 0;
    out[1] = 1;
    return 2;
}

static int leak_player(cfr_game_t *g, uint64_t key, void *u)
{
    (void)g; (void)u;
    return ((leak_state_t *)(uintptr_t)key)->to_act;
}

static uint64_t leak_apply(cfr_game_t *g, uint64_t key, int action, void *u)
{
    (void)g; (void)u;
    (void)action;
    leak_state_t *parent = (leak_state_t *)(uintptr_t)key;
    leak_state_t *child = (leak_state_t *)calloc(1, sizeof(leak_state_t));
    child->to_act = 1 - parent->to_act;
    child->depth = parent->depth + 1;
    g_live_states++;
    return (uint64_t)(uintptr_t)child;
}

static void leak_release(cfr_game_t *g, uint64_t key, void *u)
{
    (void)g; (void)u;
    leak_state_t *st = (leak_state_t *)(uintptr_t)key;
    if (st)
    {
        free(st);
        g_live_states--;
    }
}

int main(void)
{
    leak_state_t root;
    memset(&root, 0, sizeof(root));
    root.to_act = 0;
    root.depth = 0;

    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = leak_player;
    game.get_actions = leak_n_actions;
    game.apply_action = leak_apply;
    game.release_state = leak_release;
    game.is_terminal = leak_is_term;
    game.get_utility = leak_utility;
    game.initial_state = &root;
    game.state_size = sizeof(leak_state_t);
    game.num_players = 2;

    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 5;
    cfg.metrics_interval = 1;
    cfg.metrics_level = 1; /* exercises cfr_exploitability_proxy -> best response */

    double exploitability = 0.0;
    exploitability = cfr_solve(&game, storage, &cfg, &exploitability);
    (void)exploitability;

    /* Traversal + best-response must not leave child states allocated. */
    ASSERT_EQ(g_live_states, 0, "no leaked states after solve (traverse + BR)");

    /* Policy-value traversal is another apply_action consumer. */
    (void)cfr_compute_policy_value(&game, storage, 0, NULL);
    ASSERT_EQ(g_live_states, 0, "no leaked states after policy value");

    cfr_storage_destroy(storage);

    printf("CFR state-release test passed.\n");
    return 0;
}
