#include "unity.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/utils/CardConverter.h>

void setUp(void)
{
    // Initialize poker evaluation tables
    InitPokerEvalCards();
}
void tearDown(void) {}

// Test hand value constants and basic operations
static void test_handval_constants(void)
{
    // Test that HandVal_HANDTYPE_VALUE creates correct shifted values
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_NOPAIR)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TWOPAIR, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_TWOPAIR)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_TRIPS)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_STRAIGHT)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FLUSH, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FULLHOUSE, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_FULLHOUSE)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_QUADS, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_QUADS)));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH)));
}

static void test_handval_creation(void)
{
    HandVal hv;

    // Test creating a pair of Aces
    hv = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(hv));

    // Test creating a flush
    hv = HandVal_HANDTYPE_VALUE(StdRules_HandType_FLUSH);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FLUSH, HandVal_HANDTYPE(hv));

    // Test creating a straight flush
    hv = HandVal_HANDTYPE_VALUE(StdRules_HandType_STFLUSH);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

static void test_handval_comparison(void)
{
    // Test HandVal creation and equality
    HandVal pair1 = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR);
    HandVal pair2 = HandVal_HANDTYPE_VALUE(StdRules_HandType_ONEPAIR);
    TEST_ASSERT_EQUAL_INT(pair1, pair2);
}

static void test_royal_flush_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Royal Flush in Spades (A K Q J T)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

static void test_straight_flush_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Straight Flush in Hearts (9 8 7 6 5)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

static void test_four_of_a_kind_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Four of a Kind (Aces with King kicker)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_QUADS, HandVal_HANDTYPE(hv));
}

static void test_full_house_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Full House (Kings over Queens)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FULLHOUSE, HandVal_HANDTYPE(hv));
}

static void test_flush_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Flush in Diamonds (A K Q J 9)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FLUSH, HandVal_HANDTYPE(hv));
}

static void test_straight_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Straight (A K Q J T) mixed suits
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

static void test_wheel_straight_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Wheel Straight (A 2 3 4 5) mixed suits
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

static void test_three_of_a_kind_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Three of a Kind (Queens with A K kickers)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(hv));
}

static void test_two_pair_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create Two Pair (Aces and Kings with Queen kicker)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TWOPAIR, HandVal_HANDTYPE(hv));
}

static void test_one_pair_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create One Pair (Jacks with A K Q kickers)
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(hv));
}

static void test_high_card_evaluation(void)
{
    StdDeck_CardMask hand;
    HandVal hv;

    // Create High Card (A K Q J 9) mixed suits, no straight
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
}

static void test_hand_comparison_ordering(void)
{
    StdDeck_CardMask hand1, hand2;
    HandVal hv1, hv2;

    // Create Royal Flush
    StdDeck_CardMask_RESET(hand1);
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand1, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    // Create Four of a Kind
    StdDeck_CardMask_RESET(hand2);
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand2, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));

    hv1 = StdDeck_StdRules_EVAL_N(hand1, 5);
    hv2 = StdDeck_StdRules_EVAL_N(hand2, 5);

    // Royal Flush should beat Four of a Kind
    TEST_ASSERT_TRUE(hv1 > hv2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_handval_constants);
    RUN_TEST(test_handval_creation);
    RUN_TEST(test_handval_comparison);
    RUN_TEST(test_royal_flush_evaluation);
    RUN_TEST(test_straight_flush_evaluation);
    RUN_TEST(test_four_of_a_kind_evaluation);
    RUN_TEST(test_full_house_evaluation);
    RUN_TEST(test_flush_evaluation);
    RUN_TEST(test_straight_evaluation);
    RUN_TEST(test_wheel_straight_evaluation);
    RUN_TEST(test_three_of_a_kind_evaluation);
    RUN_TEST(test_two_pair_evaluation);
    RUN_TEST(test_one_pair_evaluation);
    RUN_TEST(test_high_card_evaluation);
    RUN_TEST(test_hand_comparison_ordering);
    return UNITY_END();
}
