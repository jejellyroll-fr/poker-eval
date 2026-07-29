/*
 * test_game_engine_advanced.c - Advanced Game Engine Testing
 *
 * Comprehensive testing suite for the Phase 4 advanced game engine including
 * rule validation, betting engine, and complete hand flow simulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "unity/src/unity.h"
#include <poker_eval/engine/game_engine.h>
#include <poker_eval/engine/betting_engine.h>
#include <poker_eval/core/eval_context.h>

/* Test data and helpers */
static GameEngine* test_engine = NULL;
static BettingEngine* betting_engine = NULL;
static EvalContext* eval_ctx = NULL;

/* Test hand scenarios */
typedef struct {
    const char* description;
    game_type_t game_type;
    betting_structure_t betting_structure;
    int num_players;
    int64_t starting_stacks[MAX_PLAYERS];
    int64_t small_blind;
    int64_t big_blind;
    bool expected_success;
} GameTestScenario;

static const GameTestScenario test_scenarios[] = {
    {
        "Texas Hold'em No-Limit 6-max",
        GAME_TEXAS_HOLDEM, BETTING_NO_LIMIT, 6,
        {10000, 10000, 10000, 10000, 10000, 10000, 0, 0, 0, 0},
        50, 100, true
    },
    {
        "Pot-Limit Omaha 4-max",
        GAME_OMAHA, BETTING_POT_LIMIT, 4,
        {5000, 5000, 5000, 5000, 0, 0, 0, 0, 0, 0},
        25, 50, true
    },
    {
        "Fixed-Limit Seven Stud",
        GAME_SEVEN_STUD, BETTING_LIMIT, 8,
        {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 0, 0},
        10, 20, true
    },
    {
        "Invalid - too many players",
        GAME_TEXAS_HOLDEM, BETTING_NO_LIMIT, 15,
        {1000}, 10, 20, false
    }
};

/* Setup and teardown */
void setUp(void) {
    EvalConfig cfg = eval_config_holdem();
    eval_ctx = eval_context_create(&cfg);
    TEST_ASSERT_NOT_NULL(eval_ctx);

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    test_engine = game_engine_create(eval_ctx, &rules);
    TEST_ASSERT_NOT_NULL(test_engine);

    BettingLimits limits = game_engine_get_default_limits(BETTING_NO_LIMIT);
    betting_engine = betting_engine_create(BETTING_NO_LIMIT, &limits);
    TEST_ASSERT_NOT_NULL(betting_engine);
}

void tearDown(void) {
    if (betting_engine) {
        betting_engine_destroy(betting_engine);
        betting_engine = NULL;
    }

    if (test_engine) {
        game_engine_destroy(test_engine);
        test_engine = NULL;
    }

    if (eval_ctx) {
        eval_context_destroy(eval_ctx);
        eval_ctx = NULL;
    }
}

/* Test game engine creation and configuration */
static void test_game_engine_creation(void) {
    printf("\n=== Testing game engine creation ===\n");

    /* Test with default rules */
    GameEngine* engine1 = game_engine_create(eval_ctx, NULL);
    TEST_ASSERT_NOT_NULL(engine1);
    TEST_ASSERT_EQUAL_INT(GAME_TEXAS_HOLDEM, engine1->default_rules.game_type);

    /* Test with custom rules */
    GameRules omaha_rules = game_engine_get_default_rules(GAME_OMAHA);
    GameEngine* engine2 = game_engine_create(eval_ctx, &omaha_rules);
    TEST_ASSERT_NOT_NULL(engine2);
    TEST_ASSERT_EQUAL_INT(GAME_OMAHA, engine2->default_rules.game_type);
    TEST_ASSERT_EQUAL_INT(4, engine2->default_rules.num_hole_cards);
    TEST_ASSERT_TRUE(engine2->default_rules.must_use_exactly_two);

    /* Test invalid parameters */
    GameEngine* engine3 = game_engine_create(NULL, NULL);
    TEST_ASSERT_NULL(engine3);

    game_engine_destroy(engine1);
    game_engine_destroy(engine2);

    printf("  ✓ Game engine creation working\n");
}

/* Test game state management */
static void test_game_state_management(void) {
    printf("\n=== Testing game state management ===\n");

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {50, 100, 0, 0};

    /* Test valid game state creation */
    GameState* state = game_state_create(&rules, &blinds, 6);
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_EQUAL_INT(6, state->num_players);
    TEST_ASSERT_EQUAL_INT(GAME_TEXAS_HOLDEM, state->rules.game_type);
    TEST_ASSERT_EQUAL_INT(50, state->blinds.small_blind);
    TEST_ASSERT_EQUAL_INT(100, state->blinds.big_blind);

    /* Test game state copy */
    GameState* copy = game_state_copy(state);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(state->num_players, copy->num_players);
    TEST_ASSERT_EQUAL_INT(state->rules.game_type, copy->rules.game_type);

    /* Test invalid parameters */
    GameState* invalid1 = game_state_create(NULL, &blinds, 6);
    TEST_ASSERT_NULL(invalid1);

    GameState* invalid2 = game_state_create(&rules, &blinds, 1);  /* Too few players */
    TEST_ASSERT_NULL(invalid2);

    GameState* invalid3 = game_state_create(&rules, &blinds, 15); /* Too many players */
    TEST_ASSERT_NULL(invalid3);

    game_state_destroy(state);
    game_state_destroy(copy);

    printf("  ✓ Game state management working\n");
}

/* Test complete hand setup and flow */
static void test_complete_hand_flow(void) {
    printf("\n=== Testing complete hand flow ===\n");

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {50, 100, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 6);
    TEST_ASSERT_NOT_NULL(state);

    /* Set starting stacks */
    for (int i = 0; i < 6; i++) {
        state->players[i].stack_size = 10000;
    }

    /* Test hand setup */
    bool setup_ok = game_engine_setup_hand(test_engine, state, 0);  /* Button on player 0 */
    TEST_ASSERT_TRUE(setup_ok);
    TEST_ASSERT_EQUAL_INT(1, state->hand_number);
    TEST_ASSERT_EQUAL_INT(ROUND_PREFLOP, state->current_round);

    /* Verify blinds were posted */
    TEST_ASSERT_EQUAL_INT64(50, state->players[1].bet_amount);   /* Small blind */
    TEST_ASSERT_EQUAL_INT64(100, state->players[2].bet_amount); /* Big blind */
    TEST_ASSERT_EQUAL_INT64(150, state->pot_size);              /* Total blinds */

    /* Test hole card dealing */
    bool cards_dealt = game_engine_deal_hole_cards(test_engine, state);
    TEST_ASSERT_TRUE(cards_dealt);

    /* Verify each player has 2 cards */
    for (int i = 0; i < 6; i++) {
        int card_count = StdDeck_numCards(state->players[i].hole_cards);
        TEST_ASSERT_EQUAL_INT(2, card_count);
    }

    /* Test community card dealing */
    bool flop_dealt = game_engine_deal_flop(test_engine, state);
    TEST_ASSERT_TRUE(flop_dealt);
    TEST_ASSERT_EQUAL_INT(3, state->num_community_cards_dealt);

    bool turn_dealt = game_engine_deal_turn(test_engine, state);
    TEST_ASSERT_TRUE(turn_dealt);
    TEST_ASSERT_EQUAL_INT(4, state->num_community_cards_dealt);

    bool river_dealt = game_engine_deal_river(test_engine, state);
    TEST_ASSERT_TRUE(river_dealt);
    TEST_ASSERT_EQUAL_INT(5, state->num_community_cards_dealt);

    game_state_destroy(state);

    printf("  ✓ Complete hand flow working\n");
}

/* Test betting engine integration */
static void test_betting_engine_integration(void) {
    printf("\n=== Testing betting engine integration ===\n");

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {50, 100, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 3);  /* Heads-up + 1 */
    TEST_ASSERT_NOT_NULL(state);

    /* Set stacks */
    state->players[0].stack_size = 5000;  /* Button/Small blind in 3-way */
    state->players[1].stack_size = 5000;  /* Big blind */
    state->players[2].stack_size = 5000;  /* UTG */

    /* Setup hand */
    bool setup_ok = game_engine_setup_hand(test_engine, state, 0);
    TEST_ASSERT_TRUE(setup_ok);

    /* Test action validation - UTG should be first to act */
    TEST_ASSERT_EQUAL_INT(2, state->action_on_player);  /* UTG */

    /* Get valid actions for UTG */
    BettingAction* actions = betting_engine_get_valid_actions(betting_engine, state, 2);
    TEST_ASSERT_NOT_NULL(actions);

    /* Verify UTG can fold, call 100, or raise */
    bool can_fold = false, can_call = false, can_raise = false;
    for (int i = 0; actions[i].is_valid && i < ACTION_COUNT; i++) {
        switch (actions[i].action) {
            case ACTION_FOLD:
                can_fold = true;
                break;
            case ACTION_CALL:
                can_call = true;
                TEST_ASSERT_EQUAL_INT64(100, actions[i].amount);  /* Call big blind */
                break;
            case ACTION_RAISE:
                can_raise = true;
                TEST_ASSERT_GREATER_THAN_INT64(100, actions[i].min_amount);
                break;
            case ACTION_CHECK:
                /* allowed when amount_to_call == 0 */
                break;
            case ACTION_BET:
                /* not expected preflop with blinds posted */
                break;
            case ACTION_ALL_IN:
                /* always available */
                break;
            case ACTION_COUNT:
            default:
                break;
        }
    }

    TEST_ASSERT_TRUE(can_fold);
    TEST_ASSERT_TRUE(can_call);
    TEST_ASSERT_TRUE(can_raise);

    free(actions);

    /* Test specific action validation */
    TEST_ASSERT_TRUE(betting_engine_is_action_valid(betting_engine, state, ACTION_FOLD, 0, 2));
    TEST_ASSERT_TRUE(betting_engine_is_action_valid(betting_engine, state, ACTION_CALL, 100, 2));
    TEST_ASSERT_TRUE(betting_engine_is_action_valid(betting_engine, state, ACTION_RAISE, 300, 2));
    TEST_ASSERT_FALSE(betting_engine_is_action_valid(betting_engine, state, ACTION_RAISE, 50, 2));  /* Too small */

    /* Test pot limit calculations */
    BettingEngine* pot_limit_engine = betting_engine_create(BETTING_POT_LIMIT, NULL);
    TEST_ASSERT_NOT_NULL(pot_limit_engine);

    int64_t pot_limit = betting_engine_calculate_pot_limit(pot_limit_engine, state, 2);
    TEST_ASSERT_GREATER_THAN_INT64(0, pot_limit);

    betting_engine_destroy(pot_limit_engine);
    game_state_destroy(state);

    printf("  ✓ Betting engine integration working\n");
}

/* Test action execution and game progression */
static void test_action_execution(void) {
    printf("\n=== Testing action execution ===\n");

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {25, 50, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 4);
    TEST_ASSERT_NOT_NULL(state);

    /* Set stacks */
    for (int i = 0; i < 4; i++) {
        state->players[i].stack_size = 2000;
    }

    /* Setup hand */
    bool setup_ok = game_engine_setup_hand(test_engine, state, 0);
    TEST_ASSERT_TRUE(setup_ok);

    /* Initial state check */
    TEST_ASSERT_EQUAL_INT64(75, state->pot_size);  /* Blinds posted */

    /* Execute a call action */
    PlayerAction call_action = {
        .type = ACTION_CALL,
        .amount = 50,
        .player_id = state->action_on_player,
        .round = ROUND_PREFLOP,
        .is_valid = true
    };

    bool action_valid = game_engine_validate_action(test_engine, state, &call_action);
    TEST_ASSERT_TRUE(action_valid);

    bool action_executed = game_engine_execute_action(test_engine, state, &call_action);
    TEST_ASSERT_TRUE(action_executed);

    /* Verify action results */
    PlayerState* acting_player = &state->players[call_action.player_id];
    TEST_ASSERT_EQUAL_INT64(50, acting_player->bet_amount);
    TEST_ASSERT_EQUAL_INT64(1950, acting_player->stack_size);  /* 2000 - 50 */
    TEST_ASSERT_EQUAL_INT64(125, state->pot_size);  /* 75 + 50 */
    TEST_ASSERT_EQUAL_INT(ACTION_CALL, acting_player->last_action);

    /* Execute a raise action */
    int next_player = (call_action.player_id + 1) % 4;
    PlayerAction raise_action = {
        .type = ACTION_RAISE,
        .amount = 150,
        .player_id = next_player,
        .round = ROUND_PREFLOP,
        .is_valid = true
    };

    /* Advance action to next player first */
    state->action_on_player = next_player;

    action_valid = game_engine_validate_action(test_engine, state, &raise_action);
    TEST_ASSERT_TRUE(action_valid);

    action_executed = game_engine_execute_action(test_engine, state, &raise_action);
    TEST_ASSERT_TRUE(action_executed);

    /* Verify raise results */
    PlayerState* raising_player = &state->players[next_player];
    TEST_ASSERT_EQUAL_INT64(150, raising_player->bet_amount);
    TEST_ASSERT_EQUAL_INT64(1850, raising_player->stack_size);  /* 2000 - 150 */
    TEST_ASSERT_EQUAL_INT64(275, state->pot_size);  /* 125 + 150 */
    TEST_ASSERT_EQUAL_INT(1, state->raises_this_round);

    game_state_destroy(state);

    printf("  ✓ Action execution working\n");
}

/* Test side pot calculations */
static void test_side_pot_calculations(void) {
    printf("\n=== Testing side pot calculations ===\n");

    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {10, 20, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 3);
    TEST_ASSERT_NOT_NULL(state);

    /* Set up scenario with different stack sizes */
    state->players[0].stack_size = 100;   /* Short stack */
    state->players[1].stack_size = 500;
    state->players[2].stack_size = 1000;

    /* Simulate all-in scenario */
    state->players[0].total_investment = 100;  /* All-in */
    state->players[1].total_investment = 100;  /* Call */
    state->players[2].total_investment = 200;  /* Raise */

    /* Calculate side pots */
    SidePotCalculation* calc = betting_engine_calculate_side_pots(betting_engine, state);
    TEST_ASSERT_NOT_NULL(calc);

    /* Verify side pot structure */
    TEST_ASSERT_GREATER_THAN_INT(0, calc->num_side_pots);
    TEST_ASSERT_EQUAL_INT64(400, calc->total_pot_value);  /* 100 + 100 + 200 */

    /* First side pot should be main pot (all players eligible) */
    TEST_ASSERT_EQUAL_INT(3, calc->side_pots[0].num_eligible_players);

    betting_engine_free_side_pot_calculation(calc);
    game_state_destroy(state);

    printf("  ✓ Side pot calculations working\n");
}

/* Test different game types */
static void test_different_game_types(void) {
    printf("\n=== Testing different game types ===\n");

    /* Test Omaha setup */
    GameRules omaha_rules = game_engine_get_default_rules(GAME_OMAHA);
    TEST_ASSERT_EQUAL_INT(GAME_OMAHA, omaha_rules.game_type);
    TEST_ASSERT_EQUAL_INT(4, omaha_rules.num_hole_cards);
    TEST_ASSERT_TRUE(omaha_rules.must_use_exactly_two);

    /* Test Seven Stud setup */
    GameRules stud_rules = game_engine_get_default_rules(GAME_SEVEN_STUD);
    TEST_ASSERT_EQUAL_INT(GAME_SEVEN_STUD, stud_rules.game_type);
    TEST_ASSERT_FALSE(game_engine_has_community_cards(GAME_SEVEN_STUD));

    /* Test game type helpers */
    TEST_ASSERT_TRUE(game_engine_is_holdem_variant(GAME_TEXAS_HOLDEM));
    TEST_ASSERT_FALSE(game_engine_is_holdem_variant(GAME_OMAHA));

    TEST_ASSERT_TRUE(game_engine_is_omaha_variant(GAME_OMAHA));
    TEST_ASSERT_TRUE(game_engine_is_omaha_variant(GAME_OMAHA_HILO));
    TEST_ASSERT_FALSE(game_engine_is_omaha_variant(GAME_TEXAS_HOLDEM));

    TEST_ASSERT_TRUE(game_engine_is_stud_variant(GAME_SEVEN_STUD));
    TEST_ASSERT_TRUE(game_engine_is_stud_variant(GAME_RAZZ));

    TEST_ASSERT_TRUE(game_engine_has_community_cards(GAME_TEXAS_HOLDEM));
    TEST_ASSERT_TRUE(game_engine_has_community_cards(GAME_OMAHA));
    TEST_ASSERT_FALSE(game_engine_has_community_cards(GAME_SEVEN_STUD));

    TEST_ASSERT_TRUE(game_engine_has_low_component(GAME_OMAHA_HILO));
    TEST_ASSERT_FALSE(game_engine_has_low_component(GAME_TEXAS_HOLDEM));

    printf("  ✓ Different game types working\n");
}

/* Test edge cases and error handling */
static void test_edge_cases_and_errors(void) {
    printf("\n=== Testing edge cases and error handling ===\n");

    /* Test NULL parameter handling */
    TEST_ASSERT_FALSE(game_engine_validate_action(NULL, NULL, NULL));
    TEST_ASSERT_NULL(betting_engine_get_valid_actions(NULL, NULL, 0));

    /* Test invalid player IDs */
    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {10, 20, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 4);
    TEST_ASSERT_NOT_NULL(state);

    PlayerAction invalid_action = {
        .type = ACTION_CALL,
        .amount = 20,
        .player_id = 10,  /* Invalid player ID */
        .round = ROUND_PREFLOP
    };

    TEST_ASSERT_FALSE(game_engine_validate_action(test_engine, state, &invalid_action));

    /* Test actions by inactive players */
    state->players[0].is_active = false;
    invalid_action.player_id = 0;
    TEST_ASSERT_FALSE(game_engine_validate_action(test_engine, state, &invalid_action));

    /* Test actions by folded players */
    state->players[1].has_folded = true;
    invalid_action.player_id = 1;
    TEST_ASSERT_FALSE(game_engine_validate_action(test_engine, state, &invalid_action));

    /* Test insufficient funds */
    state->players[2].stack_size = 10;
    invalid_action.player_id = 2;
    invalid_action.amount = 100;  /* More than stack */
    TEST_ASSERT_FALSE(game_engine_validate_action(test_engine, state, &invalid_action));

    game_state_destroy(state);

    printf("  ✓ Edge cases and error handling working\n");
}

/* Test scenario-based validation */
static void test_scenario_validation(void) {
    printf("\n=== Testing scenario validation ===\n");

    for (size_t i = 0; i < sizeof(test_scenarios) / sizeof(test_scenarios[0]); i++) {
        const GameTestScenario* scenario = &test_scenarios[i];

        printf("Testing scenario: %s\n", scenario->description);

        GameRules rules = game_engine_get_default_rules(scenario->game_type);
        rules.betting_structure = scenario->betting_structure;

        BlindStructure blinds = {scenario->small_blind, scenario->big_blind, 0, 0};

        GameState* state = game_state_create(&rules, &blinds, scenario->num_players);

        if (scenario->expected_success) {
            TEST_ASSERT_NOT_NULL(state);

            /* Set starting stacks */
            for (int j = 0; j < scenario->num_players; j++) {
                state->players[j].stack_size = scenario->starting_stacks[j];
            }

            /* Test hand setup */
            bool setup_ok = game_engine_setup_hand(test_engine, state, 0);
            TEST_ASSERT_TRUE(setup_ok);

            game_state_destroy(state);
        } else {
            TEST_ASSERT_NULL(state);
        }

        printf("  ✓ Scenario '%s' validated\n", scenario->description);
    }
}

/* Test utility functions */
static void test_utility_functions(void) {
    printf("\n=== Testing utility functions ===\n");

    /* Test string representations */
    TEST_ASSERT_EQUAL_STRING("Texas Hold'em", game_engine_game_type_to_string(GAME_TEXAS_HOLDEM));
    TEST_ASSERT_EQUAL_STRING("Omaha", game_engine_game_type_to_string(GAME_OMAHA));
    TEST_ASSERT_EQUAL_STRING("Unknown", game_engine_game_type_to_string(GAME_TYPE_COUNT));

    TEST_ASSERT_EQUAL_STRING("Fold", game_engine_action_to_string(ACTION_FOLD));
    TEST_ASSERT_EQUAL_STRING("Call", game_engine_action_to_string(ACTION_CALL));
    TEST_ASSERT_EQUAL_STRING("Unknown", game_engine_action_to_string(ACTION_COUNT));

    /* Test calculation helpers */
    GameRules rules = game_engine_get_default_rules(GAME_TEXAS_HOLDEM);
    BlindStructure blinds = {25, 50, 0, 0};
    GameState* state = game_state_create(&rules, &blinds, 4);
    TEST_ASSERT_NOT_NULL(state);

    for (int i = 0; i < 4; i++) {
        state->players[i].stack_size = 1000;
    }

    game_engine_setup_hand(test_engine, state, 0);

    /* Test utility calculations */
    TEST_ASSERT_EQUAL_INT(4, game_engine_get_active_player_count(state));
    TEST_ASSERT_EQUAL_INT64(75, game_engine_get_pot_size(state));
    TEST_ASSERT_FALSE(game_engine_is_hand_complete(state));

    /* Fold all but one player */
    for (int i = 0; i < 3; i++) {
        state->players[i].has_folded = true;
        state->players[i].is_active = false;
    }

    TEST_ASSERT_EQUAL_INT(1, game_engine_get_active_player_count(state));
    TEST_ASSERT_TRUE(game_engine_is_hand_complete(state));

    game_state_destroy(state);

    printf("  ✓ Utility functions working\n");
}

int main(void) {
    printf("Advanced Game Engine Test Suite\n");
    printf("===============================\n");

    UNITY_BEGIN();

    RUN_TEST(test_game_engine_creation);
    RUN_TEST(test_game_state_management);
    RUN_TEST(test_complete_hand_flow);
    RUN_TEST(test_betting_engine_integration);
    RUN_TEST(test_action_execution);
    RUN_TEST(test_side_pot_calculations);
    RUN_TEST(test_different_game_types);
    RUN_TEST(test_edge_cases_and_errors);
    RUN_TEST(test_scenario_validation);
    RUN_TEST(test_utility_functions);

    int result = UNITY_END();

    if (result == 0) {
        printf("\n🎯 All advanced game engine tests PASSED!\n");
        printf("\nGame Engine Validation Summary:\n");
        printf("✓ Game engine creation and configuration\n");
        printf("✓ Game state management and lifecycle\n");
        printf("✓ Complete hand flow (setup → dealing → actions)\n");
        printf("✓ Betting engine integration and validation\n");
        printf("✓ Action execution and game progression\n");
        printf("✓ Side pot calculations for all-in scenarios\n");
        printf("✓ Multiple game type support and rules\n");
        printf("✓ Edge case handling and error validation\n");
        printf("✓ Scenario-based testing across game types\n");
        printf("✓ Utility functions and string representations\n");
        printf("\n🎮 Phase 4.1 Advanced Game Engine: COMPLETE\n");
    } else {
        printf("\n❌ Advanced game engine tests FAILED!\n");
        printf("⚠️  Phase 4.1 implementation needs review.\n");
    }

    return result;
}
