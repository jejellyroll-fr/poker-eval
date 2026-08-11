/**
 * @file test_ismcts_pool_leak.c
 * @brief Regression test for BUG-09: ISMCTS nodes allocated past the pool
 *        (calloc fallback) were never freed once a node pool exists.
 *
 * The solver tracks live calloc-ed heap nodes in ismcts_solver_t.
 * live_heap_nodes; a node pool that cannot hold the whole tree forces the
 * calloc fallback, and reset/free must return the counter to zero.
 */

#include <poker_eval/engine/solvers/ismcts/ismcts_poker.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

/* ===== Test Cases ===== */

static int test_pool_overflow_leak(void) {
    printf("  test_pool_overflow_leak...");

    ismcts_game_interface_t* game = ismcts_holdem_create(3);
    CHECK(game != NULL, "should create 3-player game interface");

    ismcts_config_t cfg = ismcts_poker_default_config();
    /* Small pool: pool_size = max_iterations * 2 (here 32), while the flop
     * tree exceeds it, forcing the calloc fallback path. */
    cfg.max_iterations = 16;
    cfg.num_determinizations = 2;

    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");

    ismcts_game_state_t state;
    int stacks[] = {1000, 1000, 1000};
    ismcts_state_init(&state, 3, stacks, 1, 2);

    int hero_cards[] = {12, 25}; /* Ac As */
    ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);

    int board[] = {0, 13, 26}; /* 2c 3c 4c - flop */
    ismcts_state_set_board(&state, board, 3);

    ismcts_action_t best_action;
    CHECK(ismcts_poker_search(solver, &state, 0, &best_action) == 0,
          "search should succeed");

    /* Sanity: the heap fallback must actually have been exercised. */
    CHECK((uint64_t)solver->pool_size < solver->total_nodes_created,
          "test should create more nodes than the pool holds");
    CHECK(solver->live_heap_nodes > 0,
          "overflow path should allocate heap nodes");

    /* Freeing the solver walks the tree and must release every heap node. */
    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);

    printf(" PASSED\n");
    return 0;
}

static int test_reset_does_not_leak(void) {
    printf("  test_reset_does_not_leak...");

    /* A fresh solver gives the deterministic flop-tree overflow (RNG resets
     * per solver); repeated searches on one solver would not overflow after
     * the first reset. Each cycle must return to zero live heap nodes. */
    for (int cycle = 0; cycle < 3; cycle++) {
        ismcts_game_interface_t* game = ismcts_holdem_create(3);
        CHECK(game != NULL, "should create 3-player game interface");

        ismcts_config_t cfg = ismcts_poker_default_config();
        cfg.max_iterations = 16;
        cfg.num_determinizations = 2;

        ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
        CHECK(solver != NULL, "should create solver");

        ismcts_game_state_t state;
        int stacks[] = {1000, 1000, 1000};
        ismcts_state_init(&state, 3, stacks, 1, 2);

        int hero_cards[] = {12, 25};
        ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);

        int board[] = {0, 13, 26};
        ismcts_state_set_board(&state, board, 3);

        ismcts_action_t best_action;
        CHECK(ismcts_poker_search(solver, &state, 0, &best_action) == 0,
              "search should succeed");
        CHECK(solver->live_heap_nodes > 0,
              "overflow path should allocate heap nodes");

        ismcts_poker_reset(solver);
        /* Reset must free the calloc-ed overflow nodes; the leak would
         * accumulate across cycles. */
        CHECK(solver->live_heap_nodes == 0,
              "reset should free all heap-allocated nodes");

        ismcts_poker_free(solver);
        ismcts_game_interface_free(game);
    }

    printf(" PASSED\n");
    return 0;
}

int main(void) {
    printf("test_ismcts_pool_leak\n");
    int failures = 0;

    failures += test_pool_overflow_leak();
    failures += test_reset_does_not_leak();

    if (failures > 0) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}