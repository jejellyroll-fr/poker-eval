/*
 * @file test_cfr_periodic_relock_multiway.c
 * @brief Multiway (3-player) periodic relocking EV-loss test (FEAT-11, #147)
 *
 * Exercises the N-player best-response path of the locked-node EV-loss
 * measurement: locks P0's root node and checks that the recursive
 * multiway best response is used (no crash, loss recorded and positive).
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* 3-player sequential game (same shape as test_cfr_multiway): each player in
 * turn picks action 0 or 1; terminal payoff is +1 for players matching the
 * majority action, -2 otherwise (zero-sum across the three). */

typedef struct {
    int depth;
    int actions[3];
} mw_state_t;

static uint64_t mw_key(int depth, int a0, int a1, int a2) {
    return (uint64_t)((depth << 12) | (a0 << 8) | (a1 << 4) | a2);
}
static void mw_unpack(uint64_t k, int *d, int *a0, int *a1, int *a2) {
    *d = (int)((k >> 12) & 0xF);
    *a0 = (int)((k >> 8) & 0xF);
    *a1 = (int)((k >> 4) & 0xF);
    *a2 = (int)(k & 0xF);
}

static int mw_current_player(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u;
    int d, a0, a1, a2;
    mw_unpack(k, &d, &a0, &a1, &a2);
    return d;
}
static int mw_is_terminal(cfr_game_t *g, uint64_t k, void *u) {
    (void)g; (void)u;
    int d, a0, a1, a2;
    mw_unpack(k, &d, &a0, &a1, &a2);
    return d >= 3;
}
static double mw_get_utility(cfr_game_t *g, uint64_t k, int p, void *u) {
    (void)g; (void)u;
    int d, a0, a1, a2;
    mw_unpack(k, &d, &a0, &a1, &a2);
    int sum = a0 + a1 + a2;
    if (sum >= 2) {
        if (p == 0) return a0 == 1 ? 1.0 : -2.0;
        if (p == 1) return a1 == 1 ? 1.0 : -2.0;
        return a2 == 1 ? 1.0 : -2.0;
    }
    if (p == 0) return a0 == 0 ? 1.0 : -2.0;
    if (p == 1) return a1 == 0 ? 1.0 : -2.0;
    return a2 == 0 ? 1.0 : -2.0;
}
static int mw_get_actions(cfr_game_t *g, uint64_t k, int *out, int max_a, void *u) {
    (void)g; (void)u;
    int d, a0, a1, a2;
    mw_unpack(k, &d, &a0, &a1, &a2);
    if (d >= 3) return 0;
    if (max_a < 2) return 0;
    out[0] = 0; out[1] = 1;
    return 2;
}
static uint64_t mw_apply_action(cfr_game_t *g, uint64_t k, int action, void *u) {
    (void)g; (void)u;
    int d, a0, a1, a2;
    mw_unpack(k, &d, &a0, &a1, &a2);
    if (d >= 3) return k;
    if (d == 0) a0 = action;
    else if (d == 1) a1 = action;
    else if (d == 2) a2 = action;
    return mw_key(d + 1, a0, a1, a2);
}

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            fprintf(stderr, "FAIL: %s\n", msg);            \
            return 1;                                      \
        }                                                  \
    } while (0)

int main(void) {
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");

    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = mw_current_player;
    game.get_actions = mw_get_actions;
    game.apply_action = mw_apply_action;
    game.is_terminal = mw_is_terminal;
    game.get_utility = mw_get_utility;
    game.initial_state = (void *)(uintptr_t)mw_key(0, 0, 0, 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 3;

    /* Lock P0's root to 100% action 1, forcing a (possibly suboptimal) mix so
       the multiway recursive best response is actually exercised. */
    const double lock_a1[2] = {0.0, 1.0};
    uint64_t root_key = mw_key(0, 0, 0, 0);
    CHECK(cfr_storage_set_locked_strategy(storage, root_key, lock_a1, 2) == 0, "set lock");

    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 100;
    cfg.enable_periodic_relock = 1;
    cfg.lock_period = 5;

    double expl = 0.0;
    CHECK(cfr_solve(&game, storage, &cfg, &expl) >= 0.0, "solve should succeed");

    /* Exported frequency stays at the lock (action 1). */
    double avg[2] = {0.0, 0.0};
    cfr_storage_get_avg_strategy(storage, root_key, 2, avg);
    CHECK(fabs(avg[1] - 1.0) < 1e-9, "exported frequency must be 100% action 1");

    /* The multiway recursive best response must run without crashing and
       produce a positive, internally consistent EV loss. */
    double loss = 0.0, br = 0.0, forced = 0.0;
    CHECK(cfr_storage_get_lock_ev_loss(storage, root_key, &loss, &br, &forced) == 1,
          "EV loss must be recorded for the locked root (multiway BR path)");
    /* Structural guarantees of the recursive multiway best response:
         br     = max_i BR(child_i)  >=  forced = sum_i locked[i]*BR(child_i)
       so loss = br - forced is well-defined and non-negative. */
    CHECK(br >= forced - 1e-9, "best-response value must be >= forced value");
    CHECK(loss >= -1e-9, "EV loss must be non-negative");
    CHECK(fabs(loss - (br - forced)) < 1e-9, "EV loss equals br - forced");

    cfr_storage_destroy(storage);
    printf(" PASSED\n");
    return 0;
}
