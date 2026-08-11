/*
 * @file test_cfr_node_lock.c
 * @brief Tests for per-node strategy locking (FEAT-01)
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* 2-player sequential game, zero-sum, pointer-keyed states.
 * P0 acts first: fold(0) / call(1). P1 responds: a0(0) / a1(1).
 * Payoffs (P0, P1):
 *   (fold, a0) = (-1, +1)
 *   (fold, a1) = (-2, +2)
 *   (call, a0) = (+3, -3)
 *   (call, a1) = (-2, +2)
 * P1's best response to a 100% fold is a1 -> value +2.
 */

typedef struct
{
    int is_terminal;
    int player;
    double util[2];
} lock_state_t;

static lock_state_t g_root = {0, 0, {0.0, 0.0}};
static lock_state_t g_p1_fold = {0, 1, {0.0, 0.0}};
static lock_state_t g_p1_call = {0, 1, {0.0, 0.0}};
static lock_state_t g_t_fold_a0 = {1, -1, {-1.0, 1.0}};
static lock_state_t g_t_fold_a1 = {1, -1, {-2.0, 2.0}};
static lock_state_t g_t_call_a0 = {1, -1, {3.0, -3.0}};
static lock_state_t g_t_call_a1 = {1, -1, {-2.0, 2.0}};

static int lock_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    lock_state_t *st = (lock_state_t *)(uintptr_t)key;
    return st->player;
}

static int lock_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    lock_state_t *st = (lock_state_t *)(uintptr_t)key;
    return st->is_terminal;
}

static double lock_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game;
    (void)user;
    lock_state_t *st = (lock_state_t *)(uintptr_t)key;
    return st->util[player];
}

static int lock_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    (void)game;
    (void)user;
    lock_state_t *st = (lock_state_t *)(uintptr_t)key;
    if (st->is_terminal)
        return 0;
    if (max_actions < 2)
        return 0;
    out_actions[0] = 0;
    out_actions[1] = 1;
    return 2;
}

static uint64_t lock_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game;
    (void)user;
    lock_state_t *st = (lock_state_t *)(uintptr_t)key;
    if (st->is_terminal)
        return key;
    if (st == &g_root)
        return (action == 0) ? (uint64_t)(uintptr_t)&g_p1_fold : (uint64_t)(uintptr_t)&g_p1_call;
    if (st == &g_p1_fold)
        return (action == 0) ? (uint64_t)(uintptr_t)&g_t_fold_a0 : (uint64_t)(uintptr_t)&g_t_fold_a1;
    return (action == 0) ? (uint64_t)(uintptr_t)&g_t_call_a0 : (uint64_t)(uintptr_t)&g_t_call_a1;
}

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "%s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

static int solve_locked(cfr_storage_t *storage, int iterations, double *out_expl)
{
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = lock_current_player;
    game.get_actions = lock_get_actions;
    game.apply_action = lock_apply_action;
    game.is_terminal = lock_is_terminal;
    game.get_utility = lock_get_utility;
    game.initial_state = (void *)(uintptr_t)&g_root;
    game.state_size = sizeof(lock_state_t);
    game.num_players = 2;

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = iterations;

    double exploitability = 0.0;
    double result = cfr_solve(&game, storage, &cfg, &exploitability);
    if (out_expl)
        *out_expl = exploitability;
    /* cfr_solve returns the final exploitability; negative means invalid args */
    return (result >= 0.0) ? 0 : -1;
}

int main(void)
{
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");

    const double lock_fold[2] = {1.0, 0.0};
    uint64_t root_key = (uint64_t)(uintptr_t)&g_root;
    uint64_t p1_fold_key = (uint64_t)(uintptr_t)&g_p1_fold;

    /* --- Frequency stays at 100% fold after convergence --- */
    CHECK(cfr_storage_set_locked_strategy(storage, root_key, lock_fold, 2) == 0, "set lock");
    const double *locked = NULL;
    CHECK(cfr_storage_get_locked_strategy(storage, root_key, 2, &locked) == 1, "get lock");
    CHECK(locked != NULL && fabs(locked[0] - 1.0) < 1e-12, "lock value");

    double expl = 0.0;
    CHECK(solve_locked(storage, 300, &expl) == 0, "solve should succeed");

    double avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, root_key, 2, avg);
    CHECK(fabs(avg[0] - 1.0) < 1e-12, "exported frequency must stay 100% fold");

    /* avg pinned to lock, regret stays zero (never updated) */
    double full[2];
    CHECK(cfr_storage_peek_avg_strategy(storage, root_key, 2, full) == 0, "peek avg");
    CHECK(fabs(full[0] - 1.0) < 1e-12, "peek avg matches lock");

    /* Unlocked opponent node is still trained normally */
    double opp_avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, p1_fold_key, 2, opp_avg);
    double opp_sum = opp_avg[0] + opp_avg[1];
    CHECK(fabs(opp_sum - 1.0) < 1e-9, "opponent node strategy normalizes");

    /* --- Opponent exploits the lock (BR picks a1 -> +2) --- */
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = lock_current_player;
    game.get_actions = lock_get_actions;
    game.apply_action = lock_apply_action;
    game.is_terminal = lock_is_terminal;
    game.get_utility = lock_get_utility;
    game.initial_state = (void *)(uintptr_t)&g_root;
    game.state_size = sizeof(lock_state_t);
    game.num_players = 2;
    /* P1 maximizes its own utility, so against a guaranteed fold it selects
     * a1 and receives +2. */
    double br1 = cfr_best_response_value(&game, storage, 1, NULL);
    CHECK(fabs(br1 - 2.0) < 1e-9, "opponent best response must exploit the fold (+2)");

    /* --- Lock survives checkpoint round-trip (v3 format) --- */
    const char *cp = "lock_checkpoint.bin";
    CHECK(cfr_storage_save_checkpoint(storage, cp, 300) == 0, "save checkpoint");
    cfr_storage_t *storage2 = cfr_storage_create();
    CHECK(storage2 != NULL, "create second storage");
    uint64_t iter_out = 0;
    CHECK(cfr_storage_load_checkpoint(storage2, cp, &iter_out) == 0, "load checkpoint");
    CHECK(iter_out == 300, "checkpoint iteration");
    const double *locked2 = NULL;
    CHECK(cfr_storage_get_locked_strategy(storage2, root_key, 2, &locked2) == 1, "lock after reload");
    CHECK(locked2 != NULL && fabs(locked2[0] - 1.0) < 1e-12, "lock value after reload");
    double avg2[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage2, root_key, 2, avg2);
    CHECK(fabs(avg2[0] - 1.0) < 1e-12, "exported frequency after reload");
    cfr_storage_destroy(storage2);
    remove(cp);

    cfr_storage_destroy(storage);
    printf(" PASSED\n");
    return 0;
}
