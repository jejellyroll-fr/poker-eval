/**
 * test_card_converter.c
 *
 * Tests for CardConverter.c functions to improve code coverage.
 * Tests card string parsing and conversion utilities.
 */

#include "unity.h"
#include <poker_eval/core/CardConverter.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

void setUp(void) {
  /* Initialize the card lookup table */
  InitPokerEvalCards();
}

void tearDown(void) {}

/* Test: InitPokerEvalCards initializes card array */
static void test_init_poker_eval_cards(void) {
  /* Card 0 should be empty */
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(PokerEvalCards[0]));

  /* Cards 1-52 should be non-empty */
  for (int i = 1; i <= 52; i++) {
    TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(PokerEvalCards[i]));
  }
}

/* Test: PokerTrackerToPokerEval with valid IDs */
static void test_poker_tracker_valid_ids(void) {
  /* ID 1 = 2c */
  StdDeck_CardMask card1 = PokerTrackerToPokerEval(1);
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(card1));

  /* ID 13 = Ac */
  StdDeck_CardMask card13 = PokerTrackerToPokerEval(13);
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(card13));

  /* ID 52 = As */
  StdDeck_CardMask card52 = PokerTrackerToPokerEval(52);
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(card52));
}

/* Test: PokerTrackerToPokerEval with invalid IDs */
static void test_poker_tracker_invalid_ids(void) {
  /* ID 0 = invalid */
  StdDeck_CardMask card0 = PokerTrackerToPokerEval(0);
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(card0));

  /* ID 53 = invalid */
  StdDeck_CardMask card53 = PokerTrackerToPokerEval(53);
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(card53));

  /* Negative ID = invalid */
  StdDeck_CardMask cardNeg = PokerTrackerToPokerEval(-1);
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(cardNeg));
}

/* Test: TextToPokerEval with valid card strings */
static void test_text_to_poker_eval_valid(void) {
  /* Single card: Ace of spades */
  StdDeck_CardMask as = TextToPokerEval("As");
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(as));

  /* Two cards: AK of spades */
  StdDeck_CardMask ak = TextToPokerEval("AsKs");
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(ak));

  /* Pocket aces */
  StdDeck_CardMask aa = TextToPokerEval("AsAh");
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(aa));
}

/* Test: TextToPokerEval with multiple cards */
static void test_text_to_poker_eval_multiple(void) {
  /* Full board: 5 cards */
  StdDeck_CardMask board = TextToPokerEval("AsKsQsJsTs");
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(board));
}

/* Test: TextToPokerEval with empty string */
static void test_text_to_poker_eval_empty(void) {
  StdDeck_CardMask empty = TextToPokerEval("");
  TEST_ASSERT_TRUE(StdDeck_CardMask_IS_EMPTY(empty));
}

/* Test: TextToPokerEvalArray basic functionality */
static void test_text_to_poker_eval_array(void) {
  StdDeck_CardMask cards[7];

  int count = TextToPokerEvalArray("AsKs", cards);
  TEST_ASSERT_EQUAL_INT(2, count);
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(cards[0]));
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(cards[1]));
}

/* Test: TextToPokerEvalArray with max cards */
static void test_text_to_poker_eval_array_max(void) {
  StdDeck_CardMask cards[7];

  /* 7 cards = max */
  int count = TextToPokerEvalArray("AsKsQsJsTs9s8s", cards);
  TEST_ASSERT_EQUAL_INT(7, count);
}

/* Test: TextToPokerEvalArray with empty */
static void test_text_to_poker_eval_array_empty(void) {
  StdDeck_CardMask cards[7];

  int count = TextToPokerEvalArray("", cards);
  TEST_ASSERT_EQUAL_INT(0, count);
}

/* Test: Card uniqueness - each card should be different */
static void test_card_uniqueness(void) {
  /* All 52 cards should be unique */
  for (int i = 1; i <= 52; i++) {
    for (int j = i + 1; j <= 52; j++) {
      TEST_ASSERT_FALSE(
          StdDeck_CardMask_EQUAL(PokerEvalCards[i], PokerEvalCards[j]));
    }
  }
}

/* Test: Suit grouping - 13 cards per suit */
static void test_suit_grouping(void) {
  /* Cards 1-13 are clubs */
  /* Cards 14-26 are diamonds */
  /* Cards 27-39 are hearts */
  /* Cards 40-52 are spades */

  /* Verify we can combine cards from same suit */
  StdDeck_CardMask clubs;
  StdDeck_CardMask_RESET(clubs);
  for (int i = 1; i <= 13; i++) {
    StdDeck_CardMask_OR(clubs, clubs, PokerEvalCards[i]);
  }
  TEST_ASSERT_FALSE(StdDeck_CardMask_IS_EMPTY(clubs));
}

int main(void) {
  UNITY_BEGIN();

  /* InitPokerEvalCards tests */
  RUN_TEST(test_init_poker_eval_cards);

  /* PokerTrackerToPokerEval tests */
  RUN_TEST(test_poker_tracker_valid_ids);
  RUN_TEST(test_poker_tracker_invalid_ids);

  /* TextToPokerEval tests */
  RUN_TEST(test_text_to_poker_eval_valid);
  RUN_TEST(test_text_to_poker_eval_multiple);
  RUN_TEST(test_text_to_poker_eval_empty);

  /* TextToPokerEvalArray tests */
  RUN_TEST(test_text_to_poker_eval_array);
  RUN_TEST(test_text_to_poker_eval_array_max);
  RUN_TEST(test_text_to_poker_eval_array_empty);

  /* Validation tests */
  RUN_TEST(test_card_uniqueness);
  RUN_TEST(test_suit_grouping);

  return UNITY_END();
}
