/**
 * @file test_ismcts_poker.c
 * @brief Tests for generic ISMCTS poker solver
 */

#include <poker_eval/engine/solvers/ismcts/ismcts_poker.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

/* ===== Test Cases ===== */

static int test_config_defaults(void) {
    printf("  test_config_defaults...");
    
    ismcts_config_t cfg = ismcts_poker_default_config();
    
    CHECK(cfg.uct_c > 0.0, "UCT constant should be positive");
    CHECK(cfg.max_iterations > 0, "max iterations should be positive");
    CHECK(cfg.num_determinizations > 0, "determinizations should be positive");
    CHECK(cfg.random_seed != 0, "random seed should be set");
    
    printf(" PASSED\n");
    return 0;
}

static int test_state_init(void) {
    printf("  test_state_init...");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    CHECK(state.num_players == 2, "should have 2 players");
    CHECK(state.pot == 3, "pot should be SB + BB = 3");
    CHECK(state.to_call == 2, "to_call should be BB = 2");
    CHECK(state.round == 0, "should be preflop");
    CHECK(!state.is_terminal, "should not be terminal");
    
    /* Check stacks after blinds */
    CHECK(state.stacks[0] == 999, "SB player stack should be 999");
    CHECK(state.stacks[1] == 998, "BB player stack should be 998");
    
    printf(" PASSED\n");
    return 0;
}

static int test_state_set_cards(void) {
    printf("  test_state_set_cards...");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* Set hole cards: Player 0 has As Ah (cards 12, 25) */
    int p0_cards[] = {12, 25}; /* As (spades), Ah (hearts) */
    ismcts_state_set_hole_cards(&state, 0, p0_cards, 2);
    
    CHECK(state.num_hole_cards[0] == 2, "P0 should have 2 hole cards");
    CHECK(state.hole_cards[0][0] == 12, "P0 first card should be 12");
    CHECK(state.hole_cards[0][1] == 25, "P0 second card should be 25");
    
    /* Cards should be removed from deck */
    CHECK(!(state.deck_mask & (1ULL << 12)), "As should be removed from deck");
    CHECK(!(state.deck_mask & (1ULL << 25)), "Ah should be removed from deck");
    
    /* Set board: Kc Qc Jc (cards 11, 10, 9) */
    int board[] = {11, 10, 9};
    ismcts_state_set_board(&state, board, 3);
    
    CHECK(state.num_board_cards == 3, "should have 3 board cards");
    CHECK(state.round == 1, "should be flop");
    
    printf(" PASSED\n");
    return 0;
}

static int test_holdem_interface_create(void) {
    printf("  test_holdem_interface_create...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    CHECK(game->get_actions != NULL, "should have get_actions");
    CHECK(game->apply_action != NULL, "should have apply_action");
    CHECK(game->is_terminal != NULL, "should have is_terminal");
    CHECK(game->get_payoffs != NULL, "should have get_payoffs");
    CHECK(game->sample_opponent_cards != NULL, "should have sample_opponent_cards");
    CHECK(game->clone_state != NULL, "should have clone_state");
    
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_holdem_get_actions(void) {
    printf("  test_holdem_get_actions...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* UTG (first to act) should have: fold, call, raise, all-in */
    ismcts_action_t actions[ISMCTS_MAX_ACTIONS];
    int num_actions = game->get_actions(&state, actions, ISMCTS_MAX_ACTIONS, game->user_data);
    
    CHECK(num_actions >= 3, "should have at least 3 actions (fold, call, raise/all-in)");
    
    /* Check that fold is available */
    int has_fold = 0;
    int has_call = 0;
    for (int i = 0; i < num_actions; i++) {
        if (actions[i].type == ISMCTS_ACTION_FOLD) has_fold = 1;
        if (actions[i].type == ISMCTS_ACTION_CALL) has_call = 1;
    }
    CHECK(has_fold, "should have fold action");
    CHECK(has_call, "should have call action");
    
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_holdem_apply_action(void) {
    printf("  test_holdem_apply_action...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* Player 0 (UTG) calls */
    ismcts_action_t call_action = {ISMCTS_ACTION_CALL, 1}; /* Call 1 more (already posted SB) */
    game->apply_action(&state, &call_action, game->user_data);
    
    /* Now player 1 (BB) should act */
    /* Note: after call, BB can check or raise */
    
    /* Player 1 checks */
    ismcts_action_t check_action = {ISMCTS_ACTION_CHECK, 0};
    game->apply_action(&state, &check_action, game->user_data);
    
    /* Should move to flop (round 1) */
    CHECK(state.round == 1, "should be on flop after preflop action");
    
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_holdem_terminal_fold(void) {
    printf("  test_holdem_terminal_fold...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* Player 0 (UTG) folds */
    ismcts_action_t fold_action = {ISMCTS_ACTION_FOLD, 0};
    game->apply_action(&state, &fold_action, game->user_data);
    
    /* Game should be terminal */
    CHECK(game->is_terminal(&state, game->user_data), "should be terminal after fold");
    CHECK(state.num_active == 1, "should have 1 active player");
    
    /* Get payoffs - BB wins pot */
    double payoffs[ISMCTS_MAX_PLAYERS] = {0};
    game->get_payoffs(&state, payoffs, game->user_data);
    
    CHECK(payoffs[0] == 0.0, "folder should get 0");
    CHECK(payoffs[1] == 3.0, "winner should get pot (3)");
    
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_solver_create(void) {
    printf("  test_solver_create...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 100; /* Small for test */
    
    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    
    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_solver_search_basic(void) {
    printf("  test_solver_search_basic...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 20; /* Small for test */
    cfg.num_determinizations = 2;
    
    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    
    /* Set up a simple preflop scenario */
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* Give hero AA */
    int hero_cards[] = {12, 25}; /* As Ah */
    ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);
    
    /* Search for best action */
    ismcts_action_t best_action;
    int result = ismcts_poker_search(solver, &state, 0, &best_action);
    
    CHECK(result == 0, "search should succeed");
    /* With AA, should not fold */
    CHECK(best_action.type != ISMCTS_ACTION_FOLD, "should not fold AA preflop");
    
    printf(" action=%d ", best_action.type);
    
    /* Get stats */
    uint64_t iters, sims, nodes;
    ismcts_poker_get_stats(solver, &iters, &sims, &nodes);
    CHECK(iters > 0, "should have iterations");
    CHECK(sims > 0, "should have simulations");
    
    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf("PASSED\n");
    return 0;
}

static int test_solver_action_probs(void) {
    printf("  test_solver_action_probs...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 20;
    cfg.num_determinizations = 2;
    
    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    int hero_cards[] = {12, 25}; /* As Ah */
    ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);
    
    ismcts_action_t best_action;
    ismcts_poker_search(solver, &state, 0, &best_action);
    
    /* Get action probabilities */
    ismcts_action_t actions[ISMCTS_MAX_ACTIONS];
    double probs[ISMCTS_MAX_ACTIONS];
    int num = ismcts_poker_get_action_probs(solver, actions, probs, ISMCTS_MAX_ACTIONS);
    
    CHECK(num > 0, "should have action probabilities");
    
    /* Sum of probabilities should be 1 */
    double sum = 0.0;
    for (int i = 0; i < num; i++) {
        sum += probs[i];
        CHECK(probs[i] >= 0.0, "probabilities should be non-negative");
    }
    CHECK(fabs(sum - 1.0) < 0.001, "probabilities should sum to 1");
    
    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_multiway_holdem(void) {
    printf("  test_multiway_holdem...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(3);
    CHECK(game != NULL, "should create 3-player game");
    
    ismcts_config_t cfg = ismcts_poker_default_config();
    cfg.max_iterations = 10;
    cfg.num_determinizations = 1;
    
    ismcts_solver_t* solver = ismcts_poker_create(&cfg, game);
    CHECK(solver != NULL, "should create solver");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000, 1000};
    ismcts_state_init(&state, 3, stacks, 1, 2);
    
    CHECK(state.num_players == 3, "should have 3 players");
    
    int hero_cards[] = {0, 13}; /* 2c 2d - pocket pair */
    ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);
    
    ismcts_action_t best_action;
    int result = ismcts_poker_search(solver, &state, 0, &best_action);
    CHECK(result == 0, "search should succeed");
    
    ismcts_poker_free(solver);
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

static int test_determinization(void) {
    printf("  test_determinization...");
    
    ismcts_game_interface_t* game = ismcts_holdem_create(2);
    CHECK(game != NULL, "should create game interface");
    
    ismcts_game_state_t state;
    int stacks[] = {1000, 1000};
    ismcts_state_init(&state, 2, stacks, 1, 2);
    
    /* Hero (P0) has known cards */
    int hero_cards[] = {12, 25}; /* As Ah */
    ismcts_state_set_hole_cards(&state, 0, hero_cards, 2);
    
    /* Sample opponent cards */
    uint64_t rng = 12345;
    game->sample_opponent_cards(&state, 0, &rng, game->user_data);
    
    /* Opponent should now have cards */
    CHECK(state.num_hole_cards[1] == 2, "opponent should have 2 hole cards");
    CHECK(state.hole_cards[1][0] >= 0, "opponent first card should be valid");
    CHECK(state.hole_cards[1][1] >= 0, "opponent second card should be valid");
    
    /* Opponent cards should not be hero's cards or same as each other */
    CHECK(state.hole_cards[1][0] != 12, "opponent should not have As");
    CHECK(state.hole_cards[1][0] != 25, "opponent should not have Ah");
    CHECK(state.hole_cards[1][1] != 12, "opponent should not have As");
    CHECK(state.hole_cards[1][1] != 25, "opponent should not have Ah");
    CHECK(state.hole_cards[1][0] != state.hole_cards[1][1], "opponent cards should be different");
    
    ismcts_game_interface_free(game);
    printf(" PASSED\n");
    return 0;
}

int main(void) {
    printf("Running ISMCTS Poker tests...\n");
    
    int failures = 0;
    
    failures += test_config_defaults();
    failures += test_state_init();
    failures += test_state_set_cards();
    failures += test_holdem_interface_create();
    failures += test_holdem_get_actions();
    failures += test_holdem_apply_action();
    failures += test_holdem_terminal_fold();
    failures += test_solver_create();
    failures += test_solver_search_basic();
    failures += test_solver_action_probs();
    failures += test_multiway_holdem();
    failures += test_determinization();
    
    printf("\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
}
