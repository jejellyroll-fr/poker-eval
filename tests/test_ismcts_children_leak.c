/**
 * @file test_ismcts_children_leak.c
 * @brief Regression test for BUG-10: ismcts_poker_reset skipped the tree
 *        walk when a node pool exists, leaking every children array
 *        (heap-allocated via realloc) on each reset.
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

static int test_reset_frees_children_arrays(void) {
    printf("  test_reset_frees_children_arrays...");

    ismcts_game_interface_t* game = ismcts_holdem_create(3);
    CHECK(game != NULL, "should create 3-player game interface");

    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 16;
    cfg.num_determinizations = 2;

    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    CHECK(solver->node_pool != NULL, "test should use a node pool");

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

    /* Sanity: the search must actually have allocated children arrays. */
    CHECK(solver->live_children_arrays > 0,
          "search should allocate children arrays");

    ismcts_poker_reset(solver);

    /* BUG-10: reset with a pool skipped the tree walk entirely, so every
     * children array leaked. Post-fix the walk frees them all. */
    CHECK(solver->live_children_arrays == 0,
          "reset should free every children array");

    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);

    printf(" PASSED\n");
    return 0;
}

static int test_repeated_resets_do_not_accumulate(void) {
    printf("  test_repeated_resets_do_not_accumulate...");

    /* A fresh solver gives a deterministic tree (RNG resets per solver);
     * each cycle must return to zero live children arrays. */
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
        CHECK(solver->live_children_arrays > 0,
              "search should allocate children arrays");

        ismcts_poker_reset(solver);
        CHECK(solver->live_children_arrays == 0,
              "reset should free every children array");

        ismcts_poker_free(solver);
        ismcts_game_interface_free(game);
    }

    printf(" PASSED\n");
    return 0;
}

int main(void) {
    printf("test_ismcts_children_leak\n");
    int failures = 0;

    failures += test_reset_frees_children_arrays();
    failures += test_repeated_resets_do_not_accumulate();

    if (failures > 0) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
