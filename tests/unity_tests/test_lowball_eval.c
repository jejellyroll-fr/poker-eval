/**
 * test_low_eval.c
 *
 * Tests for low evaluation functions to improve code coverage.
 * Tests 2-7 lowball (Kansas City) and A-5 low evaluators.
 */

#include "unity.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/games/eval_low8.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper to add card to mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Test: 2-7 Lowball best hand (7-5-4-3-2) */
static void test_lowball27_best_hand(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* 7-5-4-3-2 off-suit = best 2-7 low */
  add_card(&hand, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_TRUE(value > 0);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball pair (bad hand in lowball) */
static void test_lowball27_pair(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Pair of 2s */
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball two pair */
static void test_lowball27_two_pair(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Two pair */
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_TWOPAIR, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball trips */
static void test_lowball27_trips(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Three of a kind */
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball straight (bad hand in 2-7) */
static void test_lowball27_straight(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* 6-high straight */
  add_card(&hand, StdDeck_Rank_6, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball flush (bad hand in 2-7) */
static void test_lowball27_flush(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* All spades, no straight */
  add_card(&hand, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  /* Flush beats straight beat, but in lowball this is actually a straight
   * flush! */
  TEST_ASSERT_TRUE(HandVal_HANDTYPE(value) >= StdRules_HandType_FLUSH);
}

/* Test: 2-7 Lowball full house */
static void test_lowball27_full_house(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Full house: 333-22 */
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_FULLHOUSE, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball quads */
static void test_lowball27_quads(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Four of a kind */
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 5);
  TEST_ASSERT_EQUAL_INT(StdRules_HandType_QUADS, HandVal_HANDTYPE(value));
}

/* Test: 2-7 Lowball with 7 cards */
static void test_lowball27_seven_cards(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* 7 cards - should pick best 5 */
  add_card(&hand, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  HandVal value = StdDeck_Lowball27_EVAL_N(hand, 7);
  TEST_ASSERT_TRUE(value > 0);
}

/* Test: A-5 Low best hand (wheel A-2-3-4-5) */
static void test_low_a5_wheel(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* Wheel: A-2-3-4-5 = best A-5 low */
  add_card(&hand, StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_SPADES);

  LowHandVal value = StdDeck_Lowball_EVAL(hand, 5);
  TEST_ASSERT_TRUE(value != LowHandVal_NOTHING);
}

/* Test: A-5 8-or-better qualifying */
static void test_low8_qualifying(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* 8-low qualifying: A-2-4-5-8 */
  add_card(&hand, StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  LowHandVal value = StdDeck_Lowball8_EVAL(hand, 5);
  TEST_ASSERT_TRUE(value != LowHandVal_NOTHING);
}

/* Test: A-5 8-or-better non-qualifying */
static void test_low8_non_qualifying(void) {
  StdDeck_CardMask hand;
  StdDeck_CardMask_RESET(hand);

  /* 9-low doesn't qualify for 8-or-better: A-2-4-5-9 */
  add_card(&hand, StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&hand, StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&hand, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
  add_card(&hand, StdDeck_Rank_9, StdDeck_Suit_SPADES);

  LowHandVal value = StdDeck_Lowball8_EVAL(hand, 5);
  TEST_ASSERT_EQUAL(LowHandVal_NOTHING, value);
}

/* Test: Hand comparison - 7-low beats 8-low in 2-7 */
static void test_lowball27_comparison(void) {
  StdDeck_CardMask hand1, hand2;

  /* Hand 1: 7-5-4-3-2 (better) */
  StdDeck_CardMask_RESET(hand1);
  add_card(&hand1, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&hand1, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&hand1, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand1, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand1, StdDeck_Rank_2, StdDeck_Suit_SPADES);

  /* Hand 2: 8-5-4-3-2 (worse) */
  StdDeck_CardMask_RESET(hand2);
  add_card(&hand2, StdDeck_Rank_8, StdDeck_Suit_SPADES);
  add_card(&hand2, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&hand2, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&hand2, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&hand2, StdDeck_Rank_2, StdDeck_Suit_HEARTS);

  HandVal val1 = StdDeck_Lowball27_EVAL_N(hand1, 5);
  HandVal val2 = StdDeck_Lowball27_EVAL_N(hand2, 5);

  /* In 2-7, lower cards are better, so val1 should be LESS than val2 */
  /* Both are no-pair, so we compare by top card */
  TEST_ASSERT_TRUE(val1 < val2);
}

int main(void) {
  UNITY_BEGIN();

  /* 2-7 Lowball tests */
  RUN_TEST(test_lowball27_best_hand);
  RUN_TEST(test_lowball27_pair);
  RUN_TEST(test_lowball27_two_pair);
  RUN_TEST(test_lowball27_trips);
  RUN_TEST(test_lowball27_straight);
  RUN_TEST(test_lowball27_flush);
  RUN_TEST(test_lowball27_full_house);
  RUN_TEST(test_lowball27_quads);
  RUN_TEST(test_lowball27_seven_cards);

  /* A-5 low tests */
  RUN_TEST(test_low_a5_wheel);
  RUN_TEST(test_low8_qualifying);
  RUN_TEST(test_low8_non_qualifying);

  /* Comparisons */
  RUN_TEST(test_lowball27_comparison);

  return UNITY_END();
}
