#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                          \
    do                                            \
    {                                             \
        if (!(cond))                               \
        {                                         \
            fprintf(stderr, "FAILED: %s\n", msg);  \
            return 1;                              \
        }                                         \
    } while (0)

/*
 * Hostile game with a cycle: apply_action keeps returning a state that
 * is never terminal, i.e. the tree never bottoms out.  Before the fix
 * the traversal recursed until the stack blew up (segfault on deep or
 * non-main threads); after the fix cfr_solve must abort cleanly and
 * return -1.0.
 */

static int cyc_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return (int)(key & 1);
}

static int cyc_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    (void)key;
    return 0; /* never terminal -> infinite descent without the guard */
}

static double cyc_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game;
    (void)key;
    (void)player;
    (void)user;
    return 0.0;
}

static int cyc_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    (void)game;
    (void)key;
    (void)user;
    int n = 1; /* single action: descent is a chain, not an exploding tree */
    for (int i = 0; i < n && i < max_actions; ++i)
        out_actions[i] = i;
    return n;
}

static uint64_t cyc_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game;
    (void)action;
    (void)user;
    /* Cycle: always bounce back to the same state. */
    return key;
}

static int run_cycle(int max_depth)
{
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = cyc_current_player;
    game.get_actions = cyc_get_actions;
    game.apply_action = cyc_apply_action;
    game.is_terminal = cyc_is_terminal;
    game.get_utility = cyc_get_utility;
    game.initial_state = (void *)(uintptr_t)0u;
    game.state_size = sizeof(uint64_t);
    game.num_players = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 5;
    cfg.max_depth = max_depth;

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "storage allocation");

    double exploitability = 0.0;
    double result = cfr_solve(&game, storage, &cfg, &exploitability);
    CHECK(result == -1.0, "cfr_solve must abort with -1.0 on a cycle");

    cfr_storage_destroy(storage);
    return 0;
}

int main(void)
{
    /* Small explicit limit makes the test fast and deterministic. */
    CHECK(run_cycle(32) == 0, "cycle abort at depth 32");

    /* Default limit (0 -> CFR_DEFAULT_MAX_DEPTH) must also abort. */
    CHECK(run_cycle(0) == 0, "cycle abort at default depth");

    printf("CFR recursion depth guard test passed.\n");
    return 0;
}