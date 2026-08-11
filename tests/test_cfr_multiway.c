/**
 * @file test_cfr_multiway.c
 * @brief Tests for multiway (3+ players) best-response and exploitability
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* Simple 3-player game for testing:
 * - Each player chooses action 0 or 1 in sequence
 * - Terminal payoffs depend on action combinations
 */

typedef struct {
    int depth;           /* 0=root, 1=after P0, 2=after P1, 3=terminal */
    int actions[3];      /* Actions taken by each player */
} multiway_state_t;

static multiway_state_t g_states[27]; /* 3^3 possible states */
static int g_state_count = 0;

static uint64_t get_state_key(int depth, int a0, int a1, int a2) {
    /* Pack state into key */
    return (uint64_t)((depth << 12) | (a0 << 8) | (a1 << 4) | a2);
}

static void unpack_state(uint64_t key, int *depth, int *a0, int *a1, int *a2) {
    *depth = (int)((key >> 12) & 0xF);
    *a0 = (int)((key >> 8) & 0xF);
    *a1 = (int)((key >> 4) & 0xF);
    *a2 = (int)(key & 0xF);
}

static int multiway_current_player(cfr_game_t *game, uint64_t key, void *user) {
    (void)game;
    (void)user;
    int depth, a0, a1, a2;
    unpack_state(key, &depth, &a0, &a1, &a2);
    return depth; /* Player 0 at depth 0, Player 1 at depth 1, etc. */
}

static int multiway_is_terminal(cfr_game_t *game, uint64_t key, void *user) {
    (void)game;
    (void)user;
    int depth, a0, a1, a2;
    unpack_state(key, &depth, &a0, &a1, &a2);
    return (depth >= 3);
}

static void *g_last_utility_user = NULL;

static double multiway_get_utility(cfr_game_t *game, uint64_t key, int player, void *user) {
    (void)game;
    g_last_utility_user = user;
    int depth, a0, a1, a2;
    unpack_state(key, &depth, &a0, &a1, &a2);
    
    /* Zero-sum: total utilities sum to 0 */
    /* Simple payoff matrix based on majority rule */
    int sum = a0 + a1 + a2;
    
    if (sum >= 2) {
        /* Majority chose action 1 */
        /* Players who chose 1 win, player who chose 0 loses */
        if (player == 0) return a0 == 1 ? 1.0 : -2.0;
        if (player == 1) return a1 == 1 ? 1.0 : -2.0;
        return a2 == 1 ? 1.0 : -2.0;
    } else {
        /* Majority chose action 0 */
        if (player == 0) return a0 == 0 ? 1.0 : -2.0;
        if (player == 1) return a1 == 0 ? 1.0 : -2.0;
        return a2 == 0 ? 1.0 : -2.0;
    }
}

static int multiway_get_actions(cfr_game_t *game, uint64_t key, int *out_actions, int max_actions, void *user) {
    (void)game;
    (void)user;
    int depth, a0, a1, a2;
    unpack_state(key, &depth, &a0, &a1, &a2);
    
    if (depth >= 3) return 0; /* Terminal */
    if (max_actions < 2) return 0;
    
    out_actions[0] = 0;
    out_actions[1] = 1;
    return 2;
}

static uint64_t multiway_apply_action(cfr_game_t *game, uint64_t key, int action, void *user) {
    (void)game;
    (void)user;
    int depth, a0, a1, a2;
    unpack_state(key, &depth, &a0, &a1, &a2);
    
    if (depth >= 3) return key; /* Already terminal */
    
    /* Apply action for current player */
    switch (depth) {
        case 0: a0 = action; break;
        case 1: a1 = action; break;
        case 2: a2 = action; break;
    }
    
    return get_state_key(depth + 1, a0, a1, a2);
}

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

#define CHECK_CLOSE(a, b, eps, msg)    \
    do {                               \
        if (fabs((a) - (b)) > (eps)) { \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b)); \
            return 1;                  \
        }                              \
    } while (0)

static int test_multiway_game_setup(void) {
    printf("  test_multiway_game_setup...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = multiway_current_player;
    game.get_actions = multiway_get_actions;
    game.apply_action = multiway_apply_action;
    game.is_terminal = multiway_is_terminal;
    game.get_utility = multiway_get_utility;
    game.num_players = 3;
    
    uint64_t root = get_state_key(0, 0, 0, 0);
    
    /* Verify root is not terminal */
    CHECK(!game.is_terminal(&game, root, NULL), "root should not be terminal");
    
    /* Verify player 0 acts at root */
    CHECK(game.current_player(&game, root, NULL) == 0, "player 0 should act at root");
    
    /* Apply action and verify state transitions */
    uint64_t after_p0 = game.apply_action(&game, root, 1, NULL);
    CHECK(game.current_player(&game, after_p0, NULL) == 1, "player 1 should act after p0");
    
    uint64_t after_p1 = game.apply_action(&game, after_p0, 0, NULL);
    CHECK(game.current_player(&game, after_p1, NULL) == 2, "player 2 should act after p1");
    
    uint64_t terminal = game.apply_action(&game, after_p1, 1, NULL);
    CHECK(game.is_terminal(&game, terminal, NULL), "should be terminal after p2 acts");
    
    printf(" PASSED\n");
    return 0;
}

static int test_multiway_utilities(void) {
    printf("  test_multiway_utilities...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.get_utility = multiway_get_utility;
    game.num_players = 3;
    
    /* Test case: all players choose 0 -> majority 0 -> choosers of 0 win */
    uint64_t all_zero = get_state_key(3, 0, 0, 0);
    CHECK_CLOSE(game.get_utility(&game, all_zero, 0, NULL), 1.0, 0.001, "p0 should win when all choose 0");
    CHECK_CLOSE(game.get_utility(&game, all_zero, 1, NULL), 1.0, 0.001, "p1 should win when all choose 0");
    CHECK_CLOSE(game.get_utility(&game, all_zero, 2, NULL), 1.0, 0.001, "p2 should win when all choose 0");
    
    /* Test case: all players choose 1 -> majority 1 -> choosers of 1 win */
    uint64_t all_one = get_state_key(3, 1, 1, 1);
    CHECK_CLOSE(game.get_utility(&game, all_one, 0, NULL), 1.0, 0.001, "p0 should win when all choose 1");
    
    /* Test case: p0=0, p1=1, p2=1 -> majority 1 -> p0 loses */
    uint64_t mixed = get_state_key(3, 0, 1, 1);
    CHECK_CLOSE(game.get_utility(&game, mixed, 0, NULL), -2.0, 0.001, "p0 should lose against majority 1");
    CHECK_CLOSE(game.get_utility(&game, mixed, 1, NULL), 1.0, 0.001, "p1 should win with majority 1");
    CHECK_CLOSE(game.get_utility(&game, mixed, 2, NULL), 1.0, 0.001, "p2 should win with majority 1");
    
    printf(" PASSED\n");
    return 0;
}

static int test_multiway_cfr_solve(void) {
    printf("  test_multiway_cfr_solve...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = multiway_current_player;
    game.get_actions = multiway_get_actions;
    game.apply_action = multiway_apply_action;
    game.is_terminal = multiway_is_terminal;
    game.get_utility = multiway_get_utility;
    game.initial_state = (void*)(uintptr_t)get_state_key(0, 0, 0, 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 3;
    
    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 100;
    
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");
    
    int sentinel = 42;
    game.game_data = &sentinel;
    
    double exploitability = 0.0;
    int result = cfr_solve(&game, storage, &cfg, &exploitability);
    CHECK(result == 0, "cfr_solve should succeed");
    
    /* Multiway exploitability must be a real metric, not the 2-player
     * proxy fallback (which returned exactly 0.0 for num_players == 3) */
    CHECK(exploitability > 0.0, "multiway exploitability should be positive");
    CHECK(g_last_utility_user == &sentinel, "solve must propagate game->game_data as user_data");
    
    cfr_storage_destroy(storage);
    printf(" PASSED (exploitability=%.4f)\n", exploitability);
    return 0;
}

static int test_multiway_best_response(void) {
    printf("  test_multiway_best_response...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = multiway_current_player;
    game.get_actions = multiway_get_actions;
    game.apply_action = multiway_apply_action;
    game.is_terminal = multiway_is_terminal;
    game.get_utility = multiway_get_utility;
    game.initial_state = (void*)(uintptr_t)get_state_key(0, 0, 0, 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 3;
    
    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 200;
    
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");
    
    double exploitability = 0.0;
    cfr_solve(&game, storage, &cfg, &exploitability);
    
    /* Test multiway best response for each player */
    double br0 = cfr_best_response_value_multiway(&game, storage, 0, NULL);
    double br1 = cfr_best_response_value_multiway(&game, storage, 1, NULL);
    double br2 = cfr_best_response_value_multiway(&game, storage, 2, NULL);
    
    printf("\n    BR values: P0=%.4f, P1=%.4f, P2=%.4f\n", br0, br1, br2);
    
    /* BR values should be bounded (game utilities are in [-2, 1]) */
    CHECK(br0 >= -2.1 && br0 <= 1.1, "BR0 should be bounded");
    CHECK(br1 >= -2.1 && br1 <= 1.1, "BR1 should be bounded");
    CHECK(br2 >= -2.1 && br2 <= 1.1, "BR2 should be bounded");
    
    cfr_storage_destroy(storage);
    printf("  PASSED\n");
    return 0;
}

static int test_multiway_exploitability(void) {
    printf("  test_multiway_exploitability...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = multiway_current_player;
    game.get_actions = multiway_get_actions;
    game.apply_action = multiway_apply_action;
    game.is_terminal = multiway_is_terminal;
    game.get_utility = multiway_get_utility;
    game.initial_state = (void*)(uintptr_t)get_state_key(0, 0, 0, 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 3;
    
    cfr_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_iterations = 500;
    
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");
    
    double exploitability = 0.0;
    cfr_solve(&game, storage, &cfg, &exploitability);
    
    /* Compute full exploitability result */
    cfr_exploitability_result_t result;
    int ret = cfr_exploitability_multiway(&game, storage, NULL, &result);
    CHECK(ret == 0, "cfr_exploitability_multiway should succeed");
    
    /* Print results */
    printf("\n");
    cfr_exploitability_print(&result);
    
    /* Verify structure */
    CHECK(result.num_players == 3, "should have 3 players");
    CHECK(result.total_exploitability >= 0.0, "total exploitability should be non-negative");
    CHECK(result.nash_distance >= 0.0, "nash distance should be non-negative");
    
    /* Nash distance should be roughly total_exploitability / num_players */
    double expected_nash = result.total_exploitability / 3.0;
    CHECK_CLOSE(result.nash_distance, expected_nash, 0.001, "nash distance formula");
    
    cfr_storage_destroy(storage);
    printf("  PASSED\n");
    return 0;
}

static int test_exploitability_convergence(void) {
    printf("  test_exploitability_convergence...");
    
    cfr_game_t game;
    memset(&game, 0, sizeof(game));
    game.current_player = multiway_current_player;
    game.get_actions = multiway_get_actions;
    game.apply_action = multiway_apply_action;
    game.is_terminal = multiway_is_terminal;
    game.get_utility = multiway_get_utility;
    game.initial_state = (void*)(uintptr_t)get_state_key(0, 0, 0, 0);
    game.state_size = sizeof(uint64_t);
    game.num_players = 3;
    
    cfr_storage_t *storage = cfr_storage_create();
    CHECK(storage != NULL, "failed to create storage");
    
    /* Run with few iterations */
    cfr_config_t cfg1;
    memset(&cfg1, 0, sizeof(cfg1));
    cfg1.max_iterations = 50;
    
    double exp1 = 0.0;
    cfr_solve(&game, storage, &cfg1, &exp1);
    
    cfr_exploitability_result_t result1;
    cfr_exploitability_multiway(&game, storage, NULL, &result1);
    
    /* Run more iterations */
    cfr_config_t cfg2;
    memset(&cfg2, 0, sizeof(cfg2));
    cfg2.max_iterations = 450; /* Additional 450 = 500 total */
    
    double exp2 = 0.0;
    cfr_solve(&game, storage, &cfg2, &exp2);
    
    cfr_exploitability_result_t result2;
    cfr_exploitability_multiway(&game, storage, NULL, &result2);
    
    printf("\n    After 50 iters: total_exp=%.4f\n", result1.total_exploitability);
    printf("    After 500 iters: total_exp=%.4f\n", result2.total_exploitability);
    
    /* Exploitability should decrease (or stay same) with more iterations */
    CHECK(result2.total_exploitability <= result1.total_exploitability + 0.1,
          "exploitability should decrease with iterations");
    
    cfr_storage_destroy(storage);
    printf("  PASSED\n");
    return 0;
}

int main(void) {
    printf("Running CFR multiway tests...\n");
    
    int failures = 0;
    
    failures += test_multiway_game_setup();
    failures += test_multiway_utilities();
    failures += test_multiway_cfr_solve();
    failures += test_multiway_best_response();
    failures += test_multiway_exploitability();
    failures += test_exploitability_convergence();
    
    printf("\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
}
