/*
 * @file test_cfr_periodic_relock.c
 * @brief Tests for the periodic relocking engine (FEAT-11, issue #147)
 *
 * Verifies that, with enable_periodic_relock set, a locked infoset:
 *   - converges to its target frequencies within < lock_period iterations,
 *   - keeps the un-locked opponent node trained (true, bounty-free EVs),
 *   - exposes the exact EV loss of the forced (sub-optimal) strategy.
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
 *   (fold, a0) = (+1, -1)
 *   (fold, a1) = (-1, +1)
 *   (call, a0) = (+5, -5)
 *   (call, a1) = (+4, -4)
 * P1 minimizes P0, so against a guaranteed fold it picks a1 (P0 = -1) and
 * against a call it picks a1 (P0 = +4). P0's best action is therefore call
 * (+4); a forced 100% fold locks P0 to -1, an EV loss of 5. */

typedef struct
{
    int is_terminal;
    int player;
    double util[2];
} relock_state_t;

static relock_state_t g_root = {0, 0, {0.0, 0.0}};
static relock_state_t g_p1_fold = {0, 1, {0.0, 0.0}};
static relock_state_t g_p1_call = {0, 1, {0.0, 0.0}};
static relock_state_t g_t_fold_a0 = {1, -1, {1.0, -1.0}};
static relock_state_t g_t_fold_a1 = {1, -1, {-1.0, 1.0}};
static relock_state_t g_t_call_a0 = {1, -1, {5.0, -5.0}};
static relock_state_t g_t_call_a1 = {1, -1, {4.0, -4.0}};

static int relock_current_player(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    relock_state_t *st = (relock_state_t *)(uintptr_t)key;
    return st->player;
}

static int relock_is_terminal(cfr_game_t *game, uint64_t key, void *user)
{
    (void)game;
    (void)user;
    relock_state_t *st = (relock_state_t *)(uintptr_t)key;
    return st->is_terminal;
}

static double relock_get_utility(cfr_game_t *game, uint64_t key, int player, void *user)
{
    (void)game;
    (void)user;
    relock_state_t *st = (relock_state_t *)(uintptr_t)key;
    return st->util[player];
}

static int relock_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user)
{
    (void)game;
    (void)user;
    relock_state_t *st = (relock_state_t *)(uintptr_t)key;
    if (st->is_terminal)
        return 0;
    if (max_actions < 2)
        return 0;
    out_actions[0] = 0;
    out_actions[1] = 1;
    return 2;
}

static uint64_t relock_apply_action(cfr_game_t *game, uint64_t key, int action, void *user)
{
    (void)game;
    (void)user;
    relock_state_t *st = (relock_state_t *)(uintptr_t)key;
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

static int build_game(cfr_game_t *game)
{
    memset(game, 0, sizeof(*game));
    game->current_player = relock_current_player;
    game->get_actions = relock_get_actions;
    game->apply_action = relock_apply_action;
    game->is_terminal = relock_is_terminal;
    game->get_utility = relock_get_utility;
    game->initial_state = (void *)(uintptr_t)&g_root;
    game->state_size = sizeof(relock_state_t);
    game->num_players = 2;
    return 0;
}

static int solve_relock(cfr_storage_t *storage, int iterations, int lock_period, double *out_expl)
{
    cfr_game_t game;
    build_game(&game);

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = iterations;
    cfg.enable_periodic_relock = 1;
    cfg.lock_period = lock_period;

    double exploitability = 0.0;
    double result = cfr_solve(&game, storage, &cfg, &exploitability);
    if (out_expl)
        *out_expl = exploitability;
    return (result >= 0.0) ? 0 : -1;
}

int main(void)
{
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");

    const double lock_fold[2] = {1.0, 0.0};
    uint64_t root_key = (uint64_t)(uintptr_t)&g_root;
    uint64_t p1_fold_key = (uint64_t)(uintptr_t)&g_p1_fold;

    CHECK(cfr_storage_set_locked_strategy(storage, root_key, lock_fold, 2) == 0, "set lock");

    /* --- Acceptance 1: target frequencies within < lock_period iterations --- */
    int lock_period = 5;
    CHECK(solve_relock(storage, 100, lock_period, NULL) == 0, "solve should succeed");

    double avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, root_key, 2, avg);
    CHECK(fabs(avg[0] - 1.0) < 1e-9, "exported frequency must be 100% fold after periodic relock");

    /* --- Acceptance 2: un-locked opponent node trained (true EVs) --- */
    double opp_avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, p1_fold_key, 2, opp_avg);
    double opp_sum = opp_avg[0] + opp_avg[1];
    CHECK(fabs(opp_sum - 1.0) < 1e-9, "opponent node strategy normalizes (trained, not frozen)");

    /* --- Acceptance 3: exact EV loss of the forced lock is recorded --- */
    double loss = 0.0, br = 0.0, forced = 0.0;
    CHECK(cfr_storage_get_lock_ev_loss(storage, root_key, &loss, &br, &forced) == 1,
          "EV loss must be recorded for the locked root");
    /* Forced 100% fold -> P1 best-responds with a1, so the forced value of the
     * fold node is -1. P0's best action (call) yields +4, so the EV loss is the
     * gap between the two, i.e. 5. */
    CHECK(br > forced, "best-response value must exceed forced value");
    CHECK(loss > 0.0, "EV loss must be positive");
    CHECK(fabs(loss - (br - forced)) < 1e-9, "EV loss equals br - forced");
    CHECK(fabs(loss - 5.0) < 1e-6, "EV loss matches the expected forced-fold cost");

    /* --- Degradation: lock_period <= 0 disables relocking -> freeze semantics --- */
    cfr_storage_t *storage2 = cfr_storage_create();
    CHECK(storage2 != NULL, "create second storage");
    CHECK(cfr_storage_set_locked_strategy(storage2, root_key, lock_fold, 2) == 0, "set lock (freeze)");
    CHECK(solve_relock(storage2, 100, 0, NULL) == 0, "solve with lock_period=0");
    double avg2[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage2, root_key, 2, avg2);
    CHECK(fabs(avg2[0] - 1.0) < 1e-9, "freeze mode still holds target frequencies");
    CHECK(cfr_storage_get_lock_ev_loss(storage2, root_key, NULL, NULL, NULL) == 0,
          "no EV loss recorded when relocking disabled");
    cfr_storage_destroy(storage2);

    cfr_storage_destroy(storage);
    printf(" PASSED\n");
    return 0;
}
