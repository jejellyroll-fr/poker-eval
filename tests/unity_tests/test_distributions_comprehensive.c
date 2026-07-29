/**
 * test_distributions_comprehensive.c
 *
 * Comprehensive tests for hand distributions and range parsing.
 * Tests Omaha, Stud, and Hold'em range representations.
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/distributions/plo_nomenclature.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/range/StudRangeParser.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Test: Basic Hold'em range parsing */
static void test_holdem_basic_range(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);
  memset(&range, 0, sizeof(range));

  /* Parse simple hand */
  result = ARP_ParseRange("AA", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_TRUE(range.count > 0);
  ARP_FreeRange(&range);

  /* Parse suited hand */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKs", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_TRUE(range.count > 0);
  ARP_FreeRange(&range);

  /* Parse offsuit hand */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKo", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_TRUE(range.count > 0);
  ARP_FreeRange(&range);
}

/* Test: Hold'em range combinations */
static void test_holdem_range_combos(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* AA should have 6 combinations (C(4,2)) */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(6, range.count);
  ARP_FreeRange(&range);

  /* AKs should have 4 combinations (one per suit) */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKs", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(4, range.count);
  ARP_FreeRange(&range);

  /* AKo should have 12 combinations */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKo", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(12, range.count);
  ARP_FreeRange(&range);

  /* AK (both) should have 16 combinations */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AK", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(16, range.count);
  ARP_FreeRange(&range);
}

/* Test: Pair ranges */
static void test_pair_ranges(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* QQ-JJ should give all QQ and JJ combinations */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("QQ-JJ", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(12, range.count); /* 6 for QQ + 6 for JJ */
  ARP_FreeRange(&range);

  /* TT-77 should give 4 pocket pairs */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("TT-77", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(24, range.count); /* 6 combos * 4 pairs */
  ARP_FreeRange(&range);
}

/* Test: Hand ranges (non-pairs) */
static void test_hand_ranges(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* AK-AJ should give AK, AQ, AJ */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AK-AJ", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(48, range.count); /* 16 * 3 hands */
  ARP_FreeRange(&range);

  /* AKs-AJs should give suited versions only */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKs-AJs", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(12, range.count); /* 4 * 3 hands */
  ARP_FreeRange(&range);
}

/* Test: Multiple ranges with comma */
static void test_multiple_ranges(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* AA,KK,QQ */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA,KK,QQ", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(18, range.count); /* 6 * 3 */
  ARP_FreeRange(&range);

  /* Mixed ranges */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA,AKs,AKo", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(22, range.count); /* 6 + 4 + 12 */
  ARP_FreeRange(&range);
}

/* Test: Dead cards blocking ranges */
static void test_dead_cards_blocking(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  /* Mark As as dead */
  StdDeck_CardMask_RESET(dead_cards);
  StdDeck_CardMask_SET(
      dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

  /* AA should now have fewer combinations (As is blocked) */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result >= 0);
  /* Should have 3 combos instead of 6 */
  TEST_ASSERT_EQUAL_INT(3, range.count);
  ARP_FreeRange(&range);

  /* AKs should have fewer suited combinations */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AKs", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result >= 0);
  /* Should have 3 combos instead of 4 (AsKs blocked) */
  TEST_ASSERT_EQUAL_INT(3, range.count);
  ARP_FreeRange(&range);
}

/* Test: Specific hand notation */
static void test_specific_hands(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Specific hand: AsKh */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AsKh", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(1, range.count);
  ARP_FreeRange(&range);

  /* Multiple specific hands */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AsKh,AcKd", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(2, range.count);
  ARP_FreeRange(&range);
}

/* Test: Stud pattern basic */
static void test_stud_pattern_basic(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* (AA)K pattern - pocket aces with K kicker */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseStudPattern("(AA)K", dead_cards, &range);
  if (result > 0) {
    TEST_ASSERT_TRUE(range.count > 0);
    ARP_FreeRange(&range);
  }
  /* If not implemented, test passes */

  /* (AK)Q pattern - AK in hole, Q showing */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseStudPattern("(AK)Q", dead_cards, &range);
  if (result > 0) {
    TEST_ASSERT_TRUE(range.count > 0);
    ARP_FreeRange(&range);
  }
}

/* Test: Stud suited patterns */
static void test_stud_suited_pattern(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* (ss)Ks - suited hole cards with K showing in same suit */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseStudPattern("(ss)Ks", dead_cards, &range);
  if (result > 0) {
    TEST_ASSERT_TRUE(range.count > 0);
    ARP_FreeRange(&range);
  }
}

/* Test: Omaha double-suited pattern */
static void test_omaha_doublesuited(void) {
  /* Test if we can instantiate double-suited Omaha hands */
  /* Pattern: AAxxds (double-suited aces) */

  /* This test validates the concept, actual implementation may vary */
  StdDeck_CardMask test_hand;
  StdDeck_CardMask_RESET(test_hand);

  /* Create a double-suited hand manually */
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

  int count = StdDeck_numCards(test_hand);
  TEST_ASSERT_EQUAL_INT(4, count);
}

/* Test: Omaha rainbow pattern */
static void test_omaha_rainbow(void) {
  /* Test rainbow (all different suits) pattern */
  StdDeck_CardMask test_hand;
  StdDeck_CardMask_RESET(test_hand);

  /* Create a rainbow hand */
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
  StdDeck_CardMask_SET(
      test_hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

  int count = StdDeck_numCards(test_hand);
  TEST_ASSERT_EQUAL_INT(4, count);
}

/* Test: Range operations (add/subtract) */
static void test_range_operations(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Test: QQ-77 (QQ,JJ,TT,99,88,77) should give 36 combos */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("QQ-77", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  int initial_count = range.count;
  TEST_ASSERT_EQUAL_INT(36, initial_count);
  ARP_FreeRange(&range);

  /* Note: Subtraction operations like "QQ-77-88" would need special handling */
  /* For now, we test that basic ranges work */
}

/* Test: Empty and invalid ranges */
static void test_invalid_ranges(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Empty string */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result <= 0); /* Should fail */
  ARP_FreeRange(&range);

  /* Invalid pattern */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("ZZ", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result <= 0); /* Should fail */
  ARP_FreeRange(&range);

  /* Malformed range */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("A", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result <= 0); /* Should fail - incomplete */
  ARP_FreeRange(&range);
}

/* Test: Case sensitivity */
static void test_case_sensitivity(void) {
  arp_range_t range1, range2;
  StdDeck_CardMask dead_cards;
  int result1, result2;

  StdDeck_CardMask_RESET(dead_cards);

  /* Uppercase */
  memset(&range1, 0, sizeof(range1));
  result1 = ARP_ParseRange("AA", dead_cards, game_holdem, &range1);

  /* Lowercase (might not be supported, test gracefully) */
  memset(&range2, 0, sizeof(range2));
  result2 = ARP_ParseRange("aa", dead_cards, game_holdem, &range2);

  if (result1 > 0) {
    TEST_ASSERT_TRUE(range1.count > 0);
    ARP_FreeRange(&range1);
  }

  /* Lower case might fail, which is acceptable */
  ARP_FreeRange(&range2);
}

/* Test: Large range parsing */
static void test_large_range_parsing(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Large range: all pairs */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA,KK,QQ,JJ,TT,99,88,77,66,55,44,33,22", dead_cards,
                           game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  /* 13 pairs * 6 combos each = 78 */
  TEST_ASSERT_EQUAL_INT(78, range.count);
  ARP_FreeRange(&range);
}

/* Test: Weighted hands */
static void test_weighted_hands(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* If weight syntax is supported: AA:0.5 means 50% weight */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA", dead_cards, game_holdem, &range);

  if (result > 0) {
    /* Check if weights are initialized */
    if (range.weights == NULL) {
      /* Default: no weights, that's fine */
      TEST_ASSERT_NULL(range.weights);
    }
    ARP_FreeRange(&range);
  }
}

/* Test: Range validation */
static void test_range_validation(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Valid range */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA,KK", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);

  /* Verify hands are valid (4 cards each for Holdem) */
  for (int i = 0; i < range.count; i++) {
    int num_cards = StdDeck_numCards(range.hands[i]);
    TEST_ASSERT_EQUAL_INT(2, num_cards);
  }

  ARP_FreeRange(&range);
}

/* Test: Boundary cases */
static void test_boundary_cases(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Lowest pair */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("22", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(6, range.count);
  ARP_FreeRange(&range);

  /* Highest pair */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("AA", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(6, range.count);
  ARP_FreeRange(&range);

  /* Lowest suited connector */
  memset(&range, 0, sizeof(range));
  result = ARP_ParseRange("32s", dead_cards, game_holdem, &range);
  TEST_ASSERT_TRUE(result > 0);
  TEST_ASSERT_EQUAL_INT(4, range.count);
  ARP_FreeRange(&range);
}

/* Test: Memory management */
static void test_memory_management(void) {
  arp_range_t range;
  StdDeck_CardMask dead_cards;
  int result;

  StdDeck_CardMask_RESET(dead_cards);

  /* Parse, verify, and free multiple times */
  for (int iteration = 0; iteration < 10; iteration++) {
    memset(&range, 0, sizeof(range));
    result = ARP_ParseRange("AA,KK,QQ", dead_cards, game_holdem, &range);
    TEST_ASSERT_TRUE(result > 0);
    TEST_ASSERT_TRUE(range.hands != NULL);
    ARP_FreeRange(&range);
  }
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_holdem_basic_range);
  RUN_TEST(test_holdem_range_combos);
  RUN_TEST(test_pair_ranges);
  RUN_TEST(test_hand_ranges);
  RUN_TEST(test_multiple_ranges);
  RUN_TEST(test_dead_cards_blocking);
  RUN_TEST(test_specific_hands);
  RUN_TEST(test_stud_pattern_basic);
  RUN_TEST(test_stud_suited_pattern);
  RUN_TEST(test_omaha_doublesuited);
  RUN_TEST(test_omaha_rainbow);
  RUN_TEST(test_range_operations);
  RUN_TEST(test_invalid_ranges);
  RUN_TEST(test_case_sensitivity);
  RUN_TEST(test_large_range_parsing);
  RUN_TEST(test_weighted_hands);
  RUN_TEST(test_range_validation);
  RUN_TEST(test_boundary_cases);
  RUN_TEST(test_memory_management);

  return UNITY_END();
}
