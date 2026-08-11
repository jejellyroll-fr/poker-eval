#include <poker_eval/engine/solvers/cfr/cfr_core.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                               \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "FAILED: %s\n", msg);      \
            return 1;                                  \
        }                                              \
    } while (0)

/* Toy 2-player game where each node exposes a fixed action count. The
   get_actions callback lies about the count (returns more than
   max_actions, or a negative error code) to exercise the clamping in
   the CFR core. */
typedef struct
{
    int depth;
} hostile_state_t;

static int hostile_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return (int)(key & 1);
}

static int hostile_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    return ((key >> 8) >= 3);
}

static double hostile_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game;
    (void)user;
    int depth = (int)((key >> 8) & 0xFF);
    (void)depth;
    int actions_sum = (int)(key & 0xF);
    return (player == 0) ? (double)actions_sum : -(double)actions_sum;
}

static int g_hostile_mode; /* 0 = oversized count, 1 = negative count */

static int hostile_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    (void)game;
    (void)user;
    int n = 2;
    if (g_hostile_mode == 0)
        n = max_actions + 5; /* claim more than the buffer allows */
    else if (g_hostile_mode == 1)
        return -1; /* signal an error */
    for (int i = 0; i < n && i < max_actions; ++i)
        out_actions[i] = i;
    return n;
}

static uint64_t hostile_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game;
    (void)user;
    int depth = (int)((key >> 8) & 0xFF);
    return (uint64_t)(((depth + 1) << 8) | (action & 0xF));
}

static int run_mode(int mode)
{
    g_hostile_mode = mode;

    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = hostile_current_player;
    game.get_actions = hostile_get_actions;
    game.apply_action = hostile_apply_action;
    game.is_terminal = hostile_is_terminal;
    game.get_utility = hostile_get_utility;
    game.initial_state = (void *)(uintptr_t)(0u | 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 20;

    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");

    double exploitability = 0.0;
    double result = cfr_solve(&game, storage, &cfg, &exploitability);
    CHECK(isfinite(result) && result >= 0.0,
          "cfr_solve should return finite non-negative exploitability");

    double br0 = cfr_best_response_value(&game, storage, 0, NULL);
    double br1 = cfr_best_response_value(&game, storage, 1, NULL);
    CHECK(isfinite(br0), "BR0 finite");
    CHECK(isfinite(br1), "BR1 finite");

    cfr_storage_destroy(storage);
    return 0;
}

int main(void)
{
    CHECK(run_mode(0) == 0, "oversized action count clamped");
    CHECK(run_mode(1) == 0, "negative action count rejected");
    printf("CFR get_actions clamping test passed.\n");
    return 0;
}
