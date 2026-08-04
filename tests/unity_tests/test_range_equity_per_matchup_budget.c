/**
 * test_range_equity_per_matchup_budget.c
 *
 * Unity tests for range_equity_per_matchup_budget(), focusing on the
 * integer-overflow fix for large matchup estimates (e.g. Omaha range-vs-range,
 * C(52,4)^2 ~= 7.3e10 > INT_MAX).
 */

#include "unity.h"
#include <poker_eval/equity/RangeEquity_internal.h>
#include <poker_eval/core/poker_defs.h>
#include <limits.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static void test_budget_larger_than_matchups(void) {
  int per = range_equity_per_matchup_budget(200000, 10.0);
  TEST_ASSERT_EQUAL_INT(20000, per);
}

static void test_budget_smaller_than_matchups_clamps_to_one(void) {
  int per = range_equity_per_matchup_budget(200000, 1e12);
  TEST_ASSERT_EQUAL_INT(1, per);
}

static void test_default_budget_zero_uses_default(void) {
  int per = range_equity_per_matchup_budget(0, 100.0);
  TEST_ASSERT_EQUAL_INT(2000, per);
}

static void test_omaha_estimate_exceeding_int_max(void) {
  /* C(52,4)^2 = 270725^2 = 73292205625 ~= 7.33e10, which exceeds INT_MAX; a
   * float-to-int conversion here would be undefined behavior. With the double
   * division, per == (int)(10000 / 7.3292205625e10) == 0 -> clamped to 1. */
  double estimate = 270725.0 * 270725.0; /* ~7.33e10 */
  char buf[64];
  snprintf(buf, sizeof(buf), "estimate=%g > INT_MAX=%d", estimate, INT_MAX);
  TEST_ASSERT_TRUE_MESSAGE(estimate > (double)INT_MAX, buf);

  int per = range_equity_per_matchup_budget(10000, estimate);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, per);
  TEST_ASSERT_LESS_OR_EQUAL_INT(10000, per);
}

static void test_single_matchup_no_divide(void) {
  int per = range_equity_per_matchup_budget(200000, 1.0);
  TEST_ASSERT_EQUAL_INT(200000, per);
}

static void test_negative_estimate_two_players_null(void) {
  /* estimate <= 1.0 leaves per unchanged */
  int per = range_equity_per_matchup_budget(123, 0.5);
  TEST_ASSERT_EQUAL_INT(123, per);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_budget_larger_than_matchups);
  RUN_TEST(test_budget_smaller_than_matchups_clamps_to_one);
  RUN_TEST(test_default_budget_zero_uses_default);
  RUN_TEST(test_omaha_estimate_exceeding_int_max);
  RUN_TEST(test_single_matchup_no_divide);
  RUN_TEST(test_negative_estimate_two_players_null);
  return UNITY_END();
}
