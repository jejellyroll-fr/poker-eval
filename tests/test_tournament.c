/*
 * test_tournament.c - Tests for tournament structure and economic calculations
 */

#include "unity.h"
#include <poker_eval/economics/tournament.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * STRUCTURE TESTS
 * ============================================================================ */

void test_tournament_structure_init(void)
{
    tournament_structure_t structure;
    tournament_structure_init(&structure);
    
    TEST_ASSERT_EQUAL_INT(0, structure.num_levels);
    TEST_ASSERT_EQUAL_INT(0, structure.current_level);
    TEST_ASSERT_FALSE(structure.is_paused);
}

void test_tournament_add_level(void)
{
    tournament_structure_t structure;
    tournament_structure_init(&structure);
    
    int idx = tournament_structure_add_level(&structure, 25, 50, 0, 0, 600);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, structure.num_levels);
    TEST_ASSERT_EQUAL_INT64(25, structure.levels[0].small_blind);
    TEST_ASSERT_EQUAL_INT64(50, structure.levels[0].big_blind);
    TEST_ASSERT_EQUAL_INT64(0, structure.levels[0].ante);
    TEST_ASSERT_EQUAL_INT(600, structure.levels[0].duration_seconds);
    TEST_ASSERT_FALSE(structure.levels[0].is_break);
    
    /* Add another level with ante */
    idx = tournament_structure_add_level(&structure, 50, 100, 10, 0, 600);
    TEST_ASSERT_EQUAL_INT(1, idx);
    TEST_ASSERT_EQUAL_INT(2, structure.num_levels);
    TEST_ASSERT_EQUAL_INT64(10, structure.levels[1].ante);
}

void test_tournament_add_break(void)
{
    tournament_structure_t structure;
    tournament_structure_init(&structure);
    
    /* Add a level first */
    tournament_structure_add_level(&structure, 25, 50, 0, 0, 600);
    
    /* Add break */
    int idx = tournament_structure_add_break(&structure, 300);
    TEST_ASSERT_EQUAL_INT(1, idx);
    TEST_ASSERT_TRUE(structure.levels[1].is_break);
    TEST_ASSERT_EQUAL_INT(300, structure.levels[1].duration_seconds);
    /* Break should copy blinds from previous level */
    TEST_ASSERT_EQUAL_INT64(25, structure.levels[1].small_blind);
    TEST_ASSERT_EQUAL_INT64(50, structure.levels[1].big_blind);
}

void test_tournament_create_turbo(void)
{
    tournament_structure_t structure;
    tournament_structure_create_turbo(&structure, 10);
    
    TEST_ASSERT_GREATER_THAN(8, structure.num_levels);
    TEST_ASSERT_EQUAL_INT64(10, structure.levels[0].small_blind);
    TEST_ASSERT_EQUAL_INT64(20, structure.levels[0].big_blind);
    TEST_ASSERT_EQUAL_INT(180, structure.levels[0].duration_seconds); /* 3 minutes */
}

void test_tournament_create_regular(void)
{
    tournament_structure_t structure;
    tournament_structure_create_regular(&structure, 25);
    
    TEST_ASSERT_GREATER_THAN(10, structure.num_levels);
    TEST_ASSERT_EQUAL_INT64(25, structure.levels[0].small_blind);
    TEST_ASSERT_EQUAL_INT(600, structure.levels[0].duration_seconds); /* 10 minutes */
}

void test_tournament_create_deep_stack(void)
{
    tournament_structure_t structure;
    tournament_structure_create_deep_stack(&structure, 25);
    
    TEST_ASSERT_GREATER_THAN(12, structure.num_levels);
    TEST_ASSERT_EQUAL_INT64(25, structure.levels[0].small_blind);
    TEST_ASSERT_EQUAL_INT(1200, structure.levels[0].duration_seconds); /* 20 minutes */
    
    /* Check BB ante format in later levels */
    bool found_bb_ante = false;
    for (int i = 0; i < structure.num_levels; i++) {
        if (structure.levels[i].big_blind_ante > 0) {
            found_bb_ante = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_bb_ante);
}

/* ============================================================================
 * STATE MANAGEMENT TESTS
 * ============================================================================ */

void test_tournament_configure(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    
    tournament_error_t err = tournament_configure(&state,
        TOURNAMENT_TYPE_FREEZEOUT,
        100,    /* buy_in */
        10000,  /* starting_chips */
        9);     /* num_players */
    
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_TYPE_FREEZEOUT, state.type);
    TEST_ASSERT_EQUAL_INT64(100, state.buy_in);
    TEST_ASSERT_EQUAL_INT64(10000, state.starting_chips);
    TEST_ASSERT_EQUAL_INT(9, state.registered_players);
    TEST_ASSERT_EQUAL_INT(9, state.players_remaining);
    TEST_ASSERT_EQUAL_INT64(90000, state.total_chips);
    TEST_ASSERT_EQUAL_INT64(900, state.prize_pool);
    TEST_ASSERT_EQUAL_INT(3, state.itm_position); /* Top 3 for 9-max */
    TEST_ASSERT_EQUAL_INT(4, state.bubble_position);
}

void test_tournament_set_payouts(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    
    double payouts[] = {50.0, 30.0, 20.0};
    tournament_error_t err = tournament_set_payouts_percentage(&state, payouts, 3);
    
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_EQUAL_INT(3, state.payouts.num_payouts);
    TEST_ASSERT_TRUE(state.payouts.use_percentages);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 50.0, state.payouts.percentages[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 30.0, state.payouts.percentages[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 20.0, state.payouts.percentages[2]);
}

void test_tournament_standard_payouts(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    
    tournament_error_t err = tournament_set_standard_payouts(&state, 9);
    
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_EQUAL_INT(3, state.payouts.num_payouts);
}

void test_tournament_start_stop(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    
    TEST_ASSERT_FALSE(state.is_running);
    
    tournament_error_t err = tournament_start(&state);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_TRUE(state.is_running);
    TEST_ASSERT_EQUAL_INT(0, state.structure.current_level);
    
    /* Can't start again */
    err = tournament_start(&state);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_ERROR_ALREADY_RUNNING, err);
    
    /* Pause/resume */
    err = tournament_pause(&state);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_TRUE(state.structure.is_paused);
    
    err = tournament_resume(&state);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_FALSE(state.structure.is_paused);
}

void test_tournament_advance_level(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    TEST_ASSERT_EQUAL_INT(0, state.structure.current_level);
    
    tournament_error_t err = tournament_advance_level(&state);
    TEST_ASSERT_EQUAL_INT(TOURNAMENT_OK, err);
    TEST_ASSERT_EQUAL_INT(1, state.structure.current_level);
}

void test_tournament_eliminate_player(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_set_standard_payouts(&state, 9);
    
    TEST_ASSERT_EQUAL_INT(9, state.players_remaining);
    TEST_ASSERT_FALSE(state.on_bubble);
    TEST_ASSERT_FALSE(state.in_the_money);
    
    /* Eliminate down to bubble */
    for (int i = 0; i < 5; i++) {
        tournament_eliminate_player(&state, i, 0);
    }
    TEST_ASSERT_EQUAL_INT(4, state.players_remaining);
    TEST_ASSERT_TRUE(state.on_bubble);
    TEST_ASSERT_FALSE(state.in_the_money);
    
    /* Bubble bursts */
    tournament_eliminate_player(&state, 5, 0);
    TEST_ASSERT_EQUAL_INT(3, state.players_remaining);
    TEST_ASSERT_FALSE(state.on_bubble);
    TEST_ASSERT_TRUE(state.in_the_money);
}

/* ============================================================================
 * BLIND/ANTE QUERY TESTS
 * ============================================================================ */

void test_tournament_get_blinds(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    TEST_ASSERT_EQUAL_INT64(25, tournament_get_small_blind(&state));
    TEST_ASSERT_EQUAL_INT64(50, tournament_get_big_blind(&state));
    
    /* Advance to level with ante */
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    
    TEST_ASSERT_GREATER_THAN(0, tournament_get_ante(&state));
}

void test_tournament_get_next_level(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    const tournament_level_t *next = tournament_get_next_level(&state);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_GREATER_THAN(50, next->big_blind); /* Should be higher than level 1 */
}

/* ============================================================================
 * PRESSURE CALCULATION TESTS
 * ============================================================================ */

void test_tournament_m_ratio(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    /* 10000 chips, 25/50 blinds = 75 orbit cost */
    /* M = 10000 / 75 ≈ 133 */
    double m = tournament_calculate_m_ratio(&state, 10000, 9);
    TEST_ASSERT_FLOAT_WITHIN(5.0, 133.0, m);
    
    /* Short stack */
    m = tournament_calculate_m_ratio(&state, 750, 9);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 10.0, m);
}

void test_tournament_q_ratio(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    
    /* Average stack = 10000, player has 10000 */
    double q = tournament_calculate_q_ratio(&state, 10000);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, q);
    
    /* Chip leader with 2x average */
    q = tournament_calculate_q_ratio(&state, 20000);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0, q);
    
    /* Short stack */
    q = tournament_calculate_q_ratio(&state, 5000);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, q);
}

void test_tournament_pressure(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    /* Average stack - low pressure (pressure < 0.5) */
    double pressure = tournament_calculate_pressure(&state, 10000);
    TEST_ASSERT_TRUE(pressure < 0.5);
    
    /* Short stack - higher pressure (pressure > 0.2) */
    pressure = tournament_calculate_pressure(&state, 2000);
    TEST_ASSERT_TRUE(pressure > 0.2);
}

void test_tournament_bubble_factor(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_set_standard_payouts(&state, 9);
    
    /* Not on bubble - factor should be ~1.0 */
    double factor = tournament_calculate_bubble_factor(&state, 10000);
    TEST_ASSERT_FLOAT_WITHIN(0.2, 1.0, factor);
    
    /* Get to bubble */
    for (int i = 0; i < 5; i++) {
        tournament_eliminate_player(&state, i, 0);
    }
    TEST_ASSERT_TRUE(state.on_bubble);
    
    /* On bubble - factor should be higher */
    factor = tournament_calculate_bubble_factor(&state, 10000);
    TEST_ASSERT_GREATER_THAN(1.0, factor);
    
    /* Short stack on bubble - even higher */
    factor = tournament_calculate_bubble_factor(&state, 3000);
    TEST_ASSERT_GREATER_THAN(1.5, factor);
}

void test_tournament_equity(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_set_standard_payouts(&state, 9);
    
    /* Prize pool = 900, total chips = 90000 */
    /* 10000 chips = 11.1% of chips */
    double equity = tournament_calculate_equity(&state, 10000);
    TEST_ASSERT_GREATER_THAN(50.0, equity);
    TEST_ASSERT_LESS_THAN(200.0, equity);
}

void test_tournament_get_payout(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_set_standard_payouts(&state, 9);
    
    /* Prize pool = 900, standard 50/30/20 */
    int64_t first = tournament_get_payout(&state, 1);
    int64_t second = tournament_get_payout(&state, 2);
    int64_t third = tournament_get_payout(&state, 3);
    int64_t fourth = tournament_get_payout(&state, 4);
    
    TEST_ASSERT_EQUAL_INT64(450, first);   /* 50% of 900 */
    TEST_ASSERT_EQUAL_INT64(270, second);  /* 30% of 900 */
    TEST_ASSERT_EQUAL_INT64(180, third);   /* 20% of 900 */
    TEST_ASSERT_EQUAL_INT64(0, fourth);    /* Not ITM */
}

/* ============================================================================
 * UTILITY TESTS
 * ============================================================================ */

void test_tournament_average_stack(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    
    TEST_ASSERT_EQUAL_INT64(10000, tournament_get_average_stack(&state));
    
    /* Eliminate some players */
    tournament_eliminate_player(&state, 0, 0);
    tournament_eliminate_player(&state, 1, 0);
    
    /* 7 players remain, same total chips */
    int64_t avg = tournament_get_average_stack(&state);
    TEST_ASSERT_EQUAL_INT64(90000 / 7, avg);
}

void test_tournament_orbit_cost(void)
{
    tournament_state_t state;
    tournament_state_init(&state);
    tournament_configure(&state, TOURNAMENT_TYPE_SNG, 100, 10000, 9);
    tournament_structure_create_turbo(&state.structure, 25);
    tournament_start(&state);
    
    /* Level 1: 25/50, no ante */
    int64_t cost = tournament_get_orbit_cost(&state, 9);
    TEST_ASSERT_EQUAL_INT64(75, cost);  /* SB + BB */
    
    /* Advance to level with antes */
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    tournament_advance_level(&state);
    
    cost = tournament_get_orbit_cost(&state, 9);
    TEST_ASSERT_GREATER_THAN(75, cost);  /* SB + BB + antes */
}

void test_tournament_error_string(void)
{
    TEST_ASSERT_NOT_NULL(tournament_error_string(TOURNAMENT_OK));
    TEST_ASSERT_NOT_NULL(tournament_error_string(TOURNAMENT_ERROR_INVALID_PARAMS));
    TEST_ASSERT_NOT_NULL(tournament_error_string(TOURNAMENT_ERROR_NOT_RUNNING));
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void)
{
    UNITY_BEGIN();
    
    /* Structure tests */
    RUN_TEST(test_tournament_structure_init);
    RUN_TEST(test_tournament_add_level);
    RUN_TEST(test_tournament_add_break);
    RUN_TEST(test_tournament_create_turbo);
    RUN_TEST(test_tournament_create_regular);
    RUN_TEST(test_tournament_create_deep_stack);
    
    /* State management tests */
    RUN_TEST(test_tournament_configure);
    RUN_TEST(test_tournament_set_payouts);
    RUN_TEST(test_tournament_standard_payouts);
    RUN_TEST(test_tournament_start_stop);
    RUN_TEST(test_tournament_advance_level);
    RUN_TEST(test_tournament_eliminate_player);
    
    /* Blind/ante query tests */
    RUN_TEST(test_tournament_get_blinds);
    RUN_TEST(test_tournament_get_next_level);
    
    /* Pressure calculation tests */
    RUN_TEST(test_tournament_m_ratio);
    RUN_TEST(test_tournament_q_ratio);
    RUN_TEST(test_tournament_pressure);
    RUN_TEST(test_tournament_bubble_factor);
    RUN_TEST(test_tournament_equity);
    RUN_TEST(test_tournament_get_payout);
    
    /* Utility tests */
    RUN_TEST(test_tournament_average_stack);
    RUN_TEST(test_tournament_orbit_cost);
    RUN_TEST(test_tournament_error_string);
    
    return UNITY_END();
}
