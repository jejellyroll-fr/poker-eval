/**
 * test_modern_combinations.c
 *
 * Tests for modern_combinations.c functions to improve code coverage.
 * Tests combinatorial generation utilities.
 */

#include "unity.h"
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/modern_combinations.h>

void setUp(void) {}
void tearDown(void) {}

/* Test: combo_binomial basic */
static void test_binomial_basic(void) {
  TEST_ASSERT_EQUAL_UINT64(1, combo_binomial(0, 0));
  TEST_ASSERT_EQUAL_UINT64(1, combo_binomial(1, 0));
  TEST_ASSERT_EQUAL_UINT64(1, combo_binomial(1, 1));
  TEST_ASSERT_EQUAL_UINT64(10, combo_binomial(5, 2));
  TEST_ASSERT_EQUAL_UINT64(10, combo_binomial(5, 3));
}

/* Test: combo_binomial for poker */
static void test_binomial_poker(void) {
  /* C(52,5) = 2,598,960 */
  TEST_ASSERT_EQUAL_UINT64(2598960, combo_binomial(52, 5));
  /* C(52,2) = 1,326 (holdem starting hands) */
  TEST_ASSERT_EQUAL_UINT64(1326, combo_binomial(52, 2));
  /* C(50,5) = 2,118,760 */
  TEST_ASSERT_EQUAL_UINT64(2118760, combo_binomial(50, 5));
}

/* Test: combo_binomial edge cases */
static void test_binomial_edge_cases(void) {
  TEST_ASSERT_EQUAL_UINT64(0, combo_binomial(5, 10)); /* k > n */
  TEST_ASSERT_EQUAL_UINT64(52, combo_binomial(52, 1));
  TEST_ASSERT_EQUAL_UINT64(1, combo_binomial(52, 52));
}

/* Test: combo_config_default */
static void test_config_default(void) {
  combo_config_t config = combo_config_default();
  TEST_ASSERT_EQUAL_INT(COMBO_MODE_EXACT, config.mode);
  TEST_ASSERT_EQUAL_INT(COMBO_OPT_BALANCED, config.optimization);
  TEST_ASSERT_EQUAL_INT(COMBO_ITER_FORWARD, config.iteration);
}

/* Test: combo_config_holdem */
static void test_config_holdem(void) {
  mask_t board = 0;
  mask_t dead = 0;

  combo_config_t config = combo_config_holdem(board, dead);
  TEST_ASSERT_EQUAL_INT(2, config.combo_size); /* 2 hole cards */
  TEST_ASSERT_EQUAL_INT(COMBO_MODE_EXACT, config.mode);
}

/* Test: combo_config_omaha */
static void test_config_omaha(void) {
  mask_t board = 0;
  mask_t dead = 0;

  combo_config_t config = combo_config_omaha(board, dead, 4);
  TEST_ASSERT_EQUAL_INT(4, config.combo_size); /* 4 hole cards */
}

/* Test: combo_config_stud */
static void test_config_stud(void) {
  mask_t dead = 0;

  combo_config_t config = combo_config_stud(dead, 7);
  TEST_ASSERT_EQUAL_INT(7, config.combo_size);
}

/* Test: combo_generator_validate_config */
static void test_validate_config(void) {
  combo_config_t config = combo_config_default();
  config.combo_size = 5;
  config.available_cards = 0xFFFFFFFFFFFFFULL; /* 52 cards */

  TEST_ASSERT_TRUE(combo_generator_validate_config(&config));
}

/* Test: combo_generator_validate_config invalid */
static void test_validate_config_invalid(void) {
  combo_config_t config = combo_config_default();
  config.combo_size = 60; /* More than possible */
  config.available_cards = 0xFFFFFFFFFFFFFULL;

  TEST_ASSERT_FALSE(combo_generator_validate_config(&config));
}

/* Test: combo_count_combinations */
static void test_count_combinations(void) {
  mask_t full_deck = 0xFFFFFFFFFFFFFULL; /* 52 cards */

  uint64_t count5 = combo_count_combinations(full_deck, 5);
  TEST_ASSERT_EQUAL_UINT64(2598960, count5);

  uint64_t count2 = combo_count_combinations(full_deck, 2);
  TEST_ASSERT_EQUAL_UINT64(1326, count2);
}

/* Test: combo_count_combinations_range */
static void test_count_combinations_range(void) {
  mask_t full_deck = 0xFFFFFFFFFFFFFULL;

  uint64_t range2to3 = combo_count_combinations_range(full_deck, 2, 3);
  /* C(52,2) + C(52,3) = 1326 + 22100 = 23426 */
  TEST_ASSERT_EQUAL_UINT64(23426, range2to3);
}

/* Test: combo_generator_create_simple */
static void test_generator_create_simple(void) {
  mask_t cards = 0x1F; /* 5 cards */

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);
  TEST_ASSERT_NOT_NULL(gen);

  uint64_t total = combo_generator_total(gen);
  TEST_ASSERT_EQUAL_UINT64(10, total); /* C(5,2) = 10 */

  combo_generator_destroy(gen);
}

/* Test: combo_generator_next */
static void test_generator_next(void) {
  mask_t cards = 0x1F; /* 5 cards (bits 0-4) */

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);
  TEST_ASSERT_NOT_NULL(gen);

  mask_t combo;
  int count = 0;
  while (combo_generator_next(gen, &combo)) {
    count++;
    if (count > 20)
      break; /* Safety */
  }

  TEST_ASSERT_EQUAL_INT(10, count);

  combo_generator_destroy(gen);
}

/* Test: combo_generator_has_next */
static void test_generator_has_next(void) {
  mask_t cards = 0x3; /* 2 cards */

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);
  TEST_ASSERT_NOT_NULL(gen);

  /* Should have exactly 1 combination */
  TEST_ASSERT_TRUE(combo_generator_has_next(gen));

  mask_t combo;
  combo_generator_next(gen, &combo);

  TEST_ASSERT_FALSE(combo_generator_has_next(gen));

  combo_generator_destroy(gen);
}

/* Test: combo_generator_reset */
static void test_generator_reset(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  mask_t combo;
  /* Generate a few */
  combo_generator_next(gen, &combo);
  combo_generator_next(gen, &combo);

  /* Reset */
  TEST_ASSERT_TRUE(combo_generator_reset(gen));

  /* Should be back to start */
  TEST_ASSERT_EQUAL_UINT64(10, combo_generator_remaining(gen));

  combo_generator_destroy(gen);
}

/* Test: combo_generator_remaining */
static void test_generator_remaining(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  TEST_ASSERT_EQUAL_UINT64(10, combo_generator_remaining(gen));

  mask_t combo;
  combo_generator_next(gen, &combo);

  TEST_ASSERT_EQUAL_UINT64(9, combo_generator_remaining(gen));

  combo_generator_destroy(gen);
}

/* Test: combo_generator_save_state and restore */
static void test_generator_save_restore(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  mask_t combo;
  combo_generator_next(gen, &combo);
  combo_generator_next(gen, &combo);

  /* Save state */
  combo_state_t state = combo_generator_save_state(gen);

  /* Generate more */
  combo_generator_next(gen, &combo);
  combo_generator_next(gen, &combo);

  /* Restore */
  TEST_ASSERT_TRUE(combo_generator_restore_state(gen, &state));
  TEST_ASSERT_EQUAL_UINT64(8, combo_generator_remaining(gen));

  combo_generator_destroy(gen);
}

/* Test: combo_generator_next_batch */
static void test_generator_next_batch(void) {
  mask_t cards = 0x1F; /* 5 cards */

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  mask_t batch[20];
  size_t count = combo_generator_next_batch(gen, batch, 20);

  TEST_ASSERT_EQUAL_INT(10, count);

  combo_generator_destroy(gen);
}

/* Test: combo_generator statistics */
static void test_generator_stats(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  mask_t combo;
  while (combo_generator_next(gen, &combo)) {
  }

  uint64_t generated, iterations, hits, misses;
  combo_generator_get_stats(gen, &generated, &iterations, &hits, &misses);

  TEST_ASSERT_EQUAL_UINT64(10, generated);

  combo_generator_destroy(gen);
}

/* Test: combo_generator_reset_stats */
static void test_generator_reset_stats(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  mask_t combo;
  combo_generator_next(gen, &combo);

  combo_generator_reset_stats(gen);

  uint64_t generated, iterations, hits, misses;
  combo_generator_get_stats(gen, &generated, &iterations, &hits, &misses);
  TEST_ASSERT_EQUAL_UINT64(0, generated);

  combo_generator_destroy(gen);
}

/* Test: combo_generator_cache_hit_rate */
static void test_generator_cache_hit_rate(void) {
  mask_t cards = 0x1F;

  combo_generator_t *gen = combo_generator_create_simple(cards, 2);

  double rate = combo_generator_cache_hit_rate(gen);
  TEST_ASSERT_TRUE(rate >= 0.0 && rate <= 1.0);

  combo_generator_destroy(gen);
}

int main(void) {
  UNITY_BEGIN();

  /* Binomial coefficient tests */
  RUN_TEST(test_binomial_basic);
  RUN_TEST(test_binomial_poker);
  RUN_TEST(test_binomial_edge_cases);

  /* Configuration tests */
  RUN_TEST(test_config_default);
  RUN_TEST(test_config_holdem);
  RUN_TEST(test_config_omaha);
  RUN_TEST(test_config_stud);
  RUN_TEST(test_validate_config);
  RUN_TEST(test_validate_config_invalid);

  /* Count combinations */
  RUN_TEST(test_count_combinations);
  RUN_TEST(test_count_combinations_range);

  /* Generator tests */
  RUN_TEST(test_generator_create_simple);
  RUN_TEST(test_generator_next);
  RUN_TEST(test_generator_has_next);
  RUN_TEST(test_generator_reset);
  RUN_TEST(test_generator_remaining);
  RUN_TEST(test_generator_save_restore);
  RUN_TEST(test_generator_next_batch);
  RUN_TEST(test_generator_stats);
  RUN_TEST(test_generator_reset_stats);
  RUN_TEST(test_generator_cache_hit_rate);

  return UNITY_END();
}
