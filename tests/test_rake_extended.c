/*
 * test_rake_extended.c - Tests for extended rake functionality
 */

#include "unity.h"
#include <poker_eval/economics/rake.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * BASIC RAKE TESTS (backwards compatibility)
 * ============================================================================ */

void test_basic_rake_apply(void)
{
    rake_config_t config = {
        .percentage = 0.05,  /* 5% */
        .cap = 3.0,
        .min_pot = 0.0,
        .no_flop_no_drop = 0
    };
    
    /* 100 pot * 5% = 5 rake, but capped at 3 */
    double result = pe_apply_rake(100.0, &config);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 97.0, result);
    
    /* 40 pot * 5% = 2 rake (under cap) */
    result = pe_apply_rake(40.0, &config);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 38.0, result);
}

void test_basic_rake_distribute(void)
{
    rake_config_t config = {
        .percentage = 0.05,
        .cap = 3.0,
        .min_pot = 0.0,
        .no_flop_no_drop = 0
    };
    
    double winnings[3];
    double rake = pe_distribute_pot_with_rake(100.0, 3, winnings, &config);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, rake);  /* Capped at 3 */
    TEST_ASSERT_FLOAT_WITHIN(0.01, 32.33, winnings[0]);  /* 97/3 */
    TEST_ASSERT_FLOAT_WITHIN(0.01, 32.33, winnings[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 32.33, winnings[2]);
}

/* ============================================================================
 * EXTENDED CONFIG TESTS
 * ============================================================================ */

void test_rake_config_init(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    
    TEST_ASSERT_EQUAL_INT(RAKE_METHOD_PERCENTAGE, config.method);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.05, config.percentage);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, config.cap);
    TEST_ASSERT_TRUE(config.no_flop_no_drop);
    TEST_ASSERT_TRUE(config.reduced_rake_headsup);
}

void test_rake_config_for_stakes_micro(void)
{
    rake_config_extended_t config;
    pe_rake_config_for_stakes(&config, 0.01);  /* 0.01/0.02 */
    
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.05, config.percentage);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.50, config.cap);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.25, config.headsup_cap);
}

void test_rake_config_for_stakes_medium(void)
{
    rake_config_extended_t config;
    pe_rake_config_for_stakes(&config, 0.50);  /* 0.50/1.00 */
    
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.05, config.percentage);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.00, config.cap);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.00, config.headsup_cap);
}

void test_rake_config_for_stakes_high(void)
{
    rake_config_extended_t config;
    pe_rake_config_for_stakes(&config, 10.0);  /* 10/20 */
    
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.04, config.percentage);  /* Lower % */
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.00, config.cap);
}

/* ============================================================================
 * EXTENDED RAKE CALCULATION TESTS
 * ============================================================================ */

void test_extended_rake_basic(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    
    rake_result_t result;
    int err = pe_calculate_rake_extended(100.0, &config, 6, true, false, &result);
    
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, result.total_rake);  /* 5% capped at 3 */
    TEST_ASSERT_FLOAT_WITHIN(0.01, 97.0, result.net_pot);
}

void test_extended_rake_no_flop_no_drop(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.no_flop_no_drop = true;
    
    rake_result_t result;
    
    /* No flop - should be no rake */
    int err = pe_calculate_rake_extended(50.0, &config, 6, false, false, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, result.total_rake);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 50.0, result.net_pot);
    
    /* Saw flop - should have rake */
    err = pe_calculate_rake_extended(50.0, &config, 6, true, false, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_GREATER_THAN(0.0, result.total_rake);
}

void test_extended_rake_headsup_cap(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.cap = 3.0;
    config.headsup_cap = 1.0;
    config.reduced_rake_headsup = true;
    
    rake_result_t result;
    
    /* Full ring - normal cap */
    pe_calculate_rake_extended(100.0, &config, 6, true, false, &result);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, result.total_rake);
    
    /* Heads up - reduced cap */
    pe_calculate_rake_extended(100.0, &config, 2, true, true, &result);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, result.total_rake);
}

void test_extended_rake_jackpot_contribution(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.jackpot_contribution = 0.01;  /* 1% */
    
    rake_result_t result;
    pe_calculate_rake_extended(100.0, &config, 6, true, false, &result);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, result.jackpot_contribution);
    /* Net pot = 100 - rake - jackpot */
    TEST_ASSERT_FLOAT_WITHIN(0.01, 96.0, result.net_pot);
}

void test_extended_rake_promo_contribution(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.promo_contribution = 0.005;  /* 0.5% */
    
    rake_result_t result;
    pe_calculate_rake_extended(100.0, &config, 6, true, false, &result);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, result.promo_contribution);
}

void test_extended_rake_effective_percentage(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    
    rake_result_t result;
    
    /* Small pot - full percentage */
    pe_calculate_rake_extended(20.0, &config, 6, true, false, &result);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.05, result.effective_percentage);
    
    /* Large pot - capped, effective % lower */
    pe_calculate_rake_extended(200.0, &config, 6, true, false, &result);
    /* 3 / 200 = 0.015 = 1.5% effective */
    TEST_ASSERT_FLOAT_WITHIN(0.005, 0.015, result.effective_percentage);
}

/* ============================================================================
 * TIERED RAKE TESTS
 * ============================================================================ */

void test_tiered_rake_add_tiers(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.method = RAKE_METHOD_TIERED;
    
    int idx = pe_rake_add_tier(&config, 20.0, 0.03, 0.60);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, config.num_tiers);
    
    idx = pe_rake_add_tier(&config, 40.0, 0.04, 0.80);
    TEST_ASSERT_EQUAL_INT(1, idx);
    
    idx = pe_rake_add_tier(&config, 100.0, 0.05, 1.00);
    TEST_ASSERT_EQUAL_INT(2, idx);
}

void test_tiered_rake_calculation(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.method = RAKE_METHOD_TIERED;
    config.cap = 5.0;  /* Overall cap */
    
    /* Tier 1: First $20 at 3% (cap 0.60) */
    pe_rake_add_tier(&config, 20.0, 0.03, 0.60);
    /* Tier 2: $20-40 at 4% (cap 0.80) */
    pe_rake_add_tier(&config, 40.0, 0.04, 0.80);
    /* Tier 3: $40+ at 5% (cap 1.00) */
    pe_rake_add_tier(&config, 100.0, 0.05, 1.00);
    
    rake_result_t result;
    
    /* $15 pot - only tier 1 applies */
    pe_calculate_rake_extended(15.0, &config, 6, true, false, &result);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.45, result.total_rake);  /* 15 * 0.03 */
    
    /* $30 pot - tier 1 + tier 2 */
    pe_calculate_rake_extended(30.0, &config, 6, true, false, &result);
    /* Tier 1: 20 * 0.03 = 0.60, Tier 2: 10 * 0.04 = 0.40 = 1.00 total */
    TEST_ASSERT_FLOAT_WITHIN(0.05, 1.00, result.total_rake);
}

/* ============================================================================
 * VIP DISCOUNT TESTS
 * ============================================================================ */

void test_vip_discount(void)
{
    vip_tier_t vip = {
        .level = 3,
        .rake_discount = 0.10,  /* 10% discount */
        .rakeback = 0.15        /* 15% rakeback */
    };
    
    double discounted = pe_apply_vip_discount(3.0, &vip);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.70, discounted);  /* 3 * 0.90 */
}

void test_vip_rakeback(void)
{
    vip_tier_t vip = {
        .level = 5,
        .rake_discount = 0.0,
        .rakeback = 0.25  /* 25% rakeback */
    };
    
    double rakeback = pe_calculate_rakeback(100.0, &vip);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 25.0, rakeback);
}

void test_vip_no_discount(void)
{
    vip_tier_t vip = {
        .level = 0,  /* No VIP */
        .rake_discount = 0.0,
        .rakeback = 0.0
    };
    
    double discounted = pe_apply_vip_discount(3.0, &vip);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.0, discounted);  /* No change */
}

/* ============================================================================
 * TIME RAKE TESTS
 * ============================================================================ */

void test_time_rake(void)
{
    rake_config_extended_t config;
    pe_rake_config_init(&config);
    config.method = RAKE_METHOD_TIME;
    config.time_collection = 5.0;      /* $5 per period */
    config.time_period_minutes = 30;   /* Every 30 minutes */
    
    /* 60 minutes = 2 periods */
    double collection = pe_calculate_time_rake(&config, 60);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, collection);
    
    /* 45 minutes = 1 period */
    collection = pe_calculate_time_rake(&config, 45);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, collection);
    
    /* 20 minutes = 0 periods */
    collection = pe_calculate_time_rake(&config, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, collection);
}

/* ============================================================================
 * UTILITY TESTS
 * ============================================================================ */

void test_round_rake_down(void)
{
    double rounded = pe_round_rake(2.567, 0.01, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 2.56, rounded);
    
    rounded = pe_round_rake(2.999, 0.01, true);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 2.99, rounded);
}

void test_round_rake_nearest(void)
{
    double rounded = pe_round_rake(2.567, 0.01, false);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 2.57, rounded);
    
    rounded = pe_round_rake(2.564, 0.01, false);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 2.56, rounded);
}

void test_standard_cap(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.50, pe_get_standard_cap(0.04));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.00, pe_get_standard_cap(0.25));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.00, pe_get_standard_cap(1.00));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.00, pe_get_standard_cap(10.00));
}

void test_standard_percentage(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.05, pe_get_standard_percentage(0.50));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.045, pe_get_standard_percentage(2.00));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.04, pe_get_standard_percentage(10.00));
}

void test_tournament_rake(void)
{
    /* Tournament pots should have no rake */
    double rake = pe_calculate_tournament_rake(1000.0, false);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, rake);
    
    rake = pe_calculate_tournament_rake(1000.0, true);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, rake);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void)
{
    UNITY_BEGIN();
    
    /* Basic rake tests */
    RUN_TEST(test_basic_rake_apply);
    RUN_TEST(test_basic_rake_distribute);
    
    /* Extended config tests */
    RUN_TEST(test_rake_config_init);
    RUN_TEST(test_rake_config_for_stakes_micro);
    RUN_TEST(test_rake_config_for_stakes_medium);
    RUN_TEST(test_rake_config_for_stakes_high);
    
    /* Extended calculation tests */
    RUN_TEST(test_extended_rake_basic);
    RUN_TEST(test_extended_rake_no_flop_no_drop);
    RUN_TEST(test_extended_rake_headsup_cap);
    RUN_TEST(test_extended_rake_jackpot_contribution);
    RUN_TEST(test_extended_rake_promo_contribution);
    RUN_TEST(test_extended_rake_effective_percentage);
    
    /* Tiered rake tests */
    RUN_TEST(test_tiered_rake_add_tiers);
    RUN_TEST(test_tiered_rake_calculation);
    
    /* VIP tests */
    RUN_TEST(test_vip_discount);
    RUN_TEST(test_vip_rakeback);
    RUN_TEST(test_vip_no_discount);
    
    /* Time rake tests */
    RUN_TEST(test_time_rake);
    
    /* Utility tests */
    RUN_TEST(test_round_rake_down);
    RUN_TEST(test_round_rake_nearest);
    RUN_TEST(test_standard_cap);
    RUN_TEST(test_standard_percentage);
    RUN_TEST(test_tournament_rake);
    
    return UNITY_END();
}
