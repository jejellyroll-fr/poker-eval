/*
 * test_ismcts_basic.c - Basic tests for ISMCTS OFC solver
 */

#include <poker_eval/ofc/solver/ismcts.h>
#include <poker_eval/ofc/ofc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Helper: Create a simple test hand */
static void setup_test_hand(ofc_hand_t* hand) {
    memset(hand, 0, sizeof(ofc_hand_t));
    for (int i = 0; i < OFC_NUM_HANDS; i++) {
        StdDeck_CardMask_RESET(hand->hands[i]);
        hand->card_count[i] = 0;
    }
    hand->is_foul = 0;
    hand->is_fantasyland = 0;
    hand->stay_fantasyland = 0;
}

/* Test 1: Config creation */
static int test_config_creation(void) {
    printf("Test 1: Config creation... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();

    assert(cfg.uct_c > 0.0);
    assert(cfg.max_iterations > 0);
    assert(cfg.max_depth == ISMCTS_DEFAULT_MAX_DEPTH);
    assert(cfg.enable_foul_pruning == true);
    (void)cfg; /* Used in assertions */

    printf("PASS\n");
    return 0;
}

/* Test 2: Solver creation and destruction */
static int test_solver_lifecycle(void) {
    printf("Test 2: Solver lifecycle... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();
    ofc_ismcts_solver_t* solver = ofc_ismcts_create(&cfg);

    assert(solver != NULL);
    assert(solver->root == NULL);
    assert(solver->total_simulations == 0);

    ofc_ismcts_free(solver);

    printf("PASS\n");
    return 0;
}

/* Test 3: Node creation */
static int test_node_creation(void) {
    printf("Test 3: Node creation... ");

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        StdDeck_CardMask_SET(unseen, i);
    }

    ofc_ismcts_node_t* node = ofc_ismcts_node_create(&hand, &unseen, 0);

    assert(node != NULL);
    assert(node->cards_placed == 0);
    assert(node->visit_count == 0);
    assert(node->total_reward == 0.0);
    assert(node->is_terminal == false);
    assert(node->num_children == 0);

    ofc_ismcts_node_free(node);

    printf("PASS\n");
    return 0;
}

/* Test 4: Node expansion */
static int test_node_expansion(void) {
    printf("Test 4: Node expansion... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();
    ofc_ismcts_solver_t* solver = ofc_ismcts_create(&cfg);

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        StdDeck_CardMask_SET(unseen, i);
    }

    ofc_ismcts_node_t* node = ofc_ismcts_node_create(&hand, &unseen, 0);

    /* Expand with first card (Ace of Spades) */
    int card = 0; /* As */
    int num_children = ofc_ismcts_expand_node(solver, node, card);

    /* Should create 3 children (top, middle, bottom) */
    assert(num_children == 3);
    assert(node->num_children == 3);

    /* Verify each child has the card placed */
    for (int i = 0; i < 3; i++) {
        assert(node->children[i] != NULL);
        assert(node->children[i]->cards_placed == 1);
        ofc_position_t pos = node->actions[i].position;
        assert(StdDeck_CardMask_CARD_IS_SET(node->children[i]->my_hand.hands[pos], card));
        (void)pos; /* Used in assertion */
    }
    (void)num_children; /* Used in assertion */

    ofc_ismcts_node_free(node);
    ofc_ismcts_free(solver);

    printf("PASS\n");
    return 0;
}

/* Test 5: UCT selection */
static int test_uct_selection(void) {
    printf("Test 5: UCT selection... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();
    ofc_ismcts_solver_t* solver = ofc_ismcts_create(&cfg);

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        StdDeck_CardMask_SET(unseen, i);
    }

    ofc_ismcts_node_t* node = ofc_ismcts_node_create(&hand, &unseen, 0);
    ofc_ismcts_expand_node(solver, node, 0);

    /* Initially all children unvisited - should select one */
    node->visit_count = 1; /* Parent must be visited */
    ofc_ismcts_node_t* selected = ofc_ismcts_select_child(node, cfg.uct_c);

    assert(selected != NULL);

    /* Visit the selected child */
    selected->visit_count = 1;
    selected->total_reward = 10.0;

    /* Select again - might pick different child */
    ofc_ismcts_node_t* selected2 = ofc_ismcts_select_child(node, cfg.uct_c);
    assert(selected2 != NULL);
    (void)selected2; /* Used in assertion */

    ofc_ismcts_node_free(node);
    ofc_ismcts_free(solver);

    printf("PASS\n");
    return 0;
}

/* Test 6: Simulation (rollout) */
static int test_simulation(void) {
    printf("Test 6: Simulation... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();
    cfg.max_depth = 5; /* Short simulation for testing */
    ofc_ismcts_solver_t* solver = ofc_ismcts_create(&cfg);

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        StdDeck_CardMask_SET(unseen, i);
    }

    ofc_ismcts_node_t* node = ofc_ismcts_node_create(&hand, &unseen, 0);

    double reward = ofc_ismcts_simulate(solver, node, 0);

    /* Reward should be non-negative for this simple test */
    assert(reward >= 0.0);
    (void)reward; /* Used in assertion */

    ofc_ismcts_node_free(node);
    ofc_ismcts_free(solver);

    printf("PASS\n");
    return 0;
}

/* Test 7: Backpropagation */
static int test_backpropagation(void) {
    printf("Test 7: Backpropagation... ");

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);

    /* Create parent and child */
    ofc_ismcts_node_t* parent = ofc_ismcts_node_create(&hand, &unseen, 0);
    ofc_ismcts_node_t* child = ofc_ismcts_node_create(&hand, &unseen, 1);
    child->parent = parent;

    /* Backpropagate reward */
    double reward = 42.0;
    ofc_ismcts_backpropagate(child, reward);

    /* Both nodes should be updated */
    assert(child->visit_count == 1);
    assert(child->total_reward == reward);
    assert(parent->visit_count == 1);
    assert(parent->total_reward == reward);

    /* Setting child->parent does not add the child to the parent's children
     * list, so freeing the parent does not reach it — free both. */
    ofc_ismcts_node_free(child);
    ofc_ismcts_node_free(parent);

    printf("PASS\n");
    return 0;
}

/* Test 8: Full search (smoke test) */
static int test_full_search(void) {
    printf("Test 8: Full search (smoke test)... ");

    ofc_ismcts_config_t cfg = ofc_ismcts_default_config();
    cfg.max_iterations = 100; /* Quick search for testing */
    ofc_ismcts_solver_t* solver = ofc_ismcts_create(&cfg);

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        StdDeck_CardMask_SET(unseen, i);
    }

    /* Find best placement for Ace of Spades */
    ofc_position_t best_pos;
    int result = ofc_ismcts_find_best_placement(solver, &hand, 0, &unseen, &best_pos);

    assert(result == 0);
    assert(best_pos >= OFC_TOP && best_pos <= OFC_BOTTOM);
    (void)result; /* Used in assertion */

    /* Check stats */
    uint64_t iters, expansions, pruned;
    ofc_ismcts_get_stats(solver, &iters, &expansions, &pruned);

    assert(iters > 0);
    assert(iters <= (uint64_t)cfg.max_iterations);

    printf("PASS (position=%d, iters=%llu)\n", (int)best_pos, (unsigned long long)iters);

    ofc_ismcts_free(solver);
    return 0;
}

/* Test 9: Foul probability estimation */
static int test_foul_estimation(void) {
    printf("Test 9: Foul probability estimation... ");

    ofc_hand_t hand;
    setup_test_hand(&hand);

    StdDeck_CardMask unseen;
    StdDeck_CardMask_RESET(unseen);

    /* Empty hand should have low foul probability */
    double prob1 = ofc_ismcts_estimate_foul_probability(&hand, &unseen);
    assert(prob1 >= 0.0 && prob1 <= 1.0);

    /* Add some cards */
    hand.card_count[OFC_TOP] = 2;
    hand.card_count[OFC_MIDDLE] = 3;
    hand.card_count[OFC_BOTTOM] = 4;

    double prob2 = ofc_ismcts_estimate_foul_probability(&hand, &unseen);
    assert(prob2 >= 0.0 && prob2 <= 1.0);

    printf("PASS (empty=%.3f, partial=%.3f)\n", prob1, prob2);
    return 0;
}

/* Main test runner */
int main(void) {
    printf("=== ISMCTS Basic Tests ===\n\n");

    int failed = 0;

    failed += test_config_creation();
    failed += test_solver_lifecycle();
    failed += test_node_creation();
    failed += test_node_expansion();
    failed += test_uct_selection();
    failed += test_simulation();
    failed += test_backpropagation();
    failed += test_full_search();
    failed += test_foul_estimation();

    printf("\n=== Results ===\n");
    if (failed == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failed);
        return 1;
    }
}
