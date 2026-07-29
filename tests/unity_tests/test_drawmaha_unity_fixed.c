#include "unity.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_drawmaha_rules_constants(void)
{
    // Test that standard hand type constants are defined
    TEST_ASSERT_TRUE(StdRules_HandType_NOPAIR >= 0);
    TEST_ASSERT_TRUE(StdRules_HandType_ONEPAIR > StdRules_HandType_NOPAIR);
    TEST_ASSERT_TRUE(StdRules_HandType_TWOPAIR > StdRules_HandType_ONEPAIR);
    TEST_ASSERT_TRUE(StdRules_HandType_TRIPS > StdRules_HandType_TWOPAIR);
    TEST_ASSERT_TRUE(StdRules_HandType_STRAIGHT > StdRules_HandType_TRIPS);
    TEST_ASSERT_TRUE(StdRules_HandType_FLUSH > StdRules_HandType_STRAIGHT);
    TEST_ASSERT_TRUE(StdRules_HandType_FULLHOUSE > StdRules_HandType_FLUSH);
    TEST_ASSERT_TRUE(StdRules_HandType_QUADS > StdRules_HandType_FULLHOUSE);
    TEST_ASSERT_TRUE(StdRules_HandType_STFLUSH > StdRules_HandType_QUADS);
}

static void test_drawmaha_basic_evaluation(void)
{
    // Test basic evaluation using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FULLHOUSE, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_straight_flush(void)
{
    // Test straight flush using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_quads(void)
{
    // Test four of a kind using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_QUADS, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv));
}

static void test_drawmaha_flush(void)
{
    // Test flush using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FLUSH, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_straight(void)
{
    // Test straight using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_trips(void)
{
    // Test three of a kind using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_TRUE(HandVal_HANDTYPE(hv) >= StdRules_HandType_TRIPS);
}

static void test_drawmaha_two_pair(void)
{
    // Test two pair using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TWOPAIR, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_one_pair(void)
{
    // Test one pair using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv));
}

static void test_drawmaha_high_card(void)
{
    // Test high card using standard 5-card evaluation
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv));
}

static void test_drawmaha_hand_comparison(void)
{
    // Test hand comparison using standard evaluation
    // Hand 1: Four of a kind
    StdDeck_CardMask hand1;
    StdDeck_CardMask_RESET(hand1);
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    // Hand 2: Full house
    StdDeck_CardMask hand2;
    StdDeck_CardMask_RESET(hand2);
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    HandVal hv1 = StdDeck_StdRules_EVAL_N(hand1, 5);
    HandVal hv2 = StdDeck_StdRules_EVAL_N(hand2, 5);

    // Four of a kind should beat full house
    TEST_ASSERT_TRUE(hv1 > hv2);
}

static void test_drawmaha_edge_cases(void)
{
    // Test with minimal valid hand
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    // Should make a straight
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

static void test_drawmaha_wheel_straight(void)
{
    // Test wheel straight (A-2-3-4-5)
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);

    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_5, HandVal_TOP_CARD(hv)); // 5-high straight
}

int main(void)
{
    UNITY_BEGIN();

    // Basic functionality tests
    RUN_TEST(test_drawmaha_rules_constants);
    RUN_TEST(test_drawmaha_basic_evaluation);

    // Hand type tests
    RUN_TEST(test_drawmaha_straight_flush);
    RUN_TEST(test_drawmaha_quads);
    RUN_TEST(test_drawmaha_flush);
    RUN_TEST(test_drawmaha_straight);
    RUN_TEST(test_drawmaha_trips);
    RUN_TEST(test_drawmaha_two_pair);
    RUN_TEST(test_drawmaha_one_pair);
    RUN_TEST(test_drawmaha_high_card);

    // Comparison and edge cases
    RUN_TEST(test_drawmaha_hand_comparison);
    RUN_TEST(test_drawmaha_edge_cases);
    RUN_TEST(test_drawmaha_wheel_straight);

    return UNITY_END();
}
