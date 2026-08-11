/**
 * @file test_ismcts_pool_cap.c
 * @brief Regression test for BUG-11: ISMCTS pool size computed in int
 *        overflowed for max_iterations > INT_MAX/2, and the calloc was
 *        unchecked, leaving pool_size stale on failure.
 *
 * Guarded state after fix:
 *   - pool_size = min((size_t)max_iterations * 2, ISMCTS_MAX_POOL_ITEMS)
 *   - max_iterations <= 0  => no pool (pool_size == 0)
 *   - calloc failure       => pool_size == 0 (clean calloc fallback)
 */

#include <poker_eval/engine/solvers/ismcts/ismcts_poker.h>
#include <limits.h>
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

static int test_pool_size_normal(void) {
    printf("  test_pool_size_normal...");

    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");

    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 100;

    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");

    CHECK(solver->pool_size == 200, "pool should be max_iterations * 2");
    CHECK(solver->node_pool != NULL, "pool should be allocated");
    CHECK(solver->pool_used == 0, "pool should start unused");

    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_pool_size_overflow_capped(void) {
    printf("  test_pool_size_overflow_capped...");

    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");

    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = INT_MAX; /* int math would overflow to negative */

    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");

    /* Fix: pool must be capped, positive, and the allocation must exist. */
    CHECK(solver->pool_size > 0, "pool_size should not overflow/negative");
    CHECK(solver->pool_size <= ISMCTS_MAX_POOL_ITEMS,
          "pool_size should be capped");
    CHECK((uint64_t)solver->pool_size * sizeof(ismcts_node_t) > 0,
          "allocated size should be sane");
    CHECK(solver->node_pool != NULL, "pool should be allocated");

    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_no_pool_for_non_positive_iterations(void) {
    printf("  test_no_pool_for_non_positive_iterations...");

    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");

    /* zero iterations: no pool needed */
    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 0;
    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    CHECK(solver->pool_size == 0, "zero iterations => pool_size == 0");
    ismcts_poker_free(solver);

    /* negative iterations: no pool needed */
    cfg.max_iterations = -1;
    solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver with negative iterations");
    CHECK(solver->pool_size == 0, "negative iterations => pool_size == 0");
    ismcts_poker_free(solver);

    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_search_still_works_small_pool(void) {
    printf("  test_search_still_works_small_pool...");

    ismcts_game_interface_t* game = ismcts_holdem_create(3);
    CHECK(game != NULL, "should create 3-player game interface");

    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 16;
    cfg.num_determinizations = 2;

    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    CHECK(solver->node_pool != NULL, "node pool should be allocated");

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
    CHECK((uint64_t)solver->total_nodes_created > 0,
          "search should create nodes");

    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

int main(void) {
    printf("test_ismcts_pool_cap\n");
    int failures = 0;

    failures += test_pool_size_normal();
    failures += test_pool_size_overflow_capped();
    failures += test_no_pool_for_non_positive_iterations();
    failures += test_search_still_works_small_pool();

    if (failures > 0) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}