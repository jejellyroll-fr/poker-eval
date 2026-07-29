#include "unity.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Helper function to create a hand from rank/suit pairs
static StdDeck_CardMask create_hand(int r1, int s1, int r2, int s2, int r3, int s3)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r1, s1));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r2, s2));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r3, s3));
    return hand;
}

static void test_pineapple_rules_constants(void)
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

static void test_pineapple_three_card_evaluation(void)
{
    // Test three-card hand evaluation using standard evaluation
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    TEST_ASSERT_NOT_EQUAL(0, hv);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv));
}

static void test_pineapple_trips(void)
{
    // Test three of a kind in Pineapple
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_KING, HandVal_TOP_CARD(hv));
}

static void test_pineapple_straight(void)
{
    // Test that 3-card hands are evaluated as high card in standard poker
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS,
        StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    // 3-card hands are evaluated as high card in standard poker
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
}

static void test_pineapple_flush(void)
{
    // Test that 3-card flushes are evaluated as high card in standard poker
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    // 3-card hands are evaluated as high card in standard poker
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
}

static void test_pineapple_straight_flush(void)
{
    // Test that 3-card straight flushes are evaluated as high card in standard poker
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    // 3-card hands are evaluated as high card in standard poker
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
}

static void test_pineapple_high_card(void)
{
    // Test high card in Pineapple
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv));
}

static void test_pineapple_hand_comparison(void)
{
    // Test hand comparison in Pineapple
    StdDeck_CardMask trips = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask pair = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    HandVal trips_hv = StdDeck_StdRules_EVAL_N(trips, 3);
    HandVal pair_hv = StdDeck_StdRules_EVAL_N(pair, 3);

    TEST_ASSERT_TRUE(trips_hv > pair_hv);
}

static void test_pineapple_with_board(void)
{
    // Test Pineapple evaluation with board cards
    StdDeck_CardMask hole_cards = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    // Combine hole cards and board
    StdDeck_CardMask combined;
    StdDeck_CardMask_OR(combined, hole_cards, board);

    HandVal hv = StdDeck_StdRules_EVAL_N(combined, 5);
    TEST_ASSERT_NOT_EQUAL(0, hv);
    // Should make a straight (A-K-Q-J-T)
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

static void test_pineapple_edge_cases(void)
{
    // Test with empty hand
    StdDeck_CardMask empty;
    StdDeck_CardMask_RESET(empty);

    HandVal hv = StdDeck_StdRules_EVAL_N(empty, 0);
    TEST_ASSERT_EQUAL(0, hv); // Should return 0 for empty hand

    // Test with single card
    StdDeck_CardMask single;
    StdDeck_CardMask_RESET(single);
    StdDeck_CardMask_SET(single, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));

    hv = StdDeck_StdRules_EVAL_N(single, 1);
    TEST_ASSERT_NOT_EQUAL(0, hv);

    // Test with two cards
    StdDeck_CardMask two_cards;
    StdDeck_CardMask_RESET(two_cards);
    StdDeck_CardMask_SET(two_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(two_cards, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    hv = StdDeck_StdRules_EVAL_N(two_cards, 2);
    TEST_ASSERT_NOT_EQUAL(0, hv);
}

static void test_pineapple_wheel_straight(void)
{
    // Test that 3-card wheel is evaluated as high card in standard poker
    StdDeck_CardMask hand = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_2, StdDeck_Suit_HEARTS,
        StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 3);
    // 3-card hands are evaluated as high card in standard poker
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
    TEST_ASSERT_EQUAL_INT(StdDeck_Rank_ACE, HandVal_TOP_CARD(hv)); // Ace high
}

static void test_pineapple_different_suits(void)
{
    // Test that suits don't affect non-flush hands
    StdDeck_CardMask hand1 = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask hand2 = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
        StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES);

    HandVal hv1 = StdDeck_StdRules_EVAL_N(hand1, 3);
    HandVal hv2 = StdDeck_StdRules_EVAL_N(hand2, 3);

    // Both should be pairs of aces with king kicker
    TEST_ASSERT_EQUAL_INT(HandVal_HANDTYPE(hv1), HandVal_HANDTYPE(hv2));
    TEST_ASSERT_EQUAL_INT(HandVal_TOP_CARD(hv1), HandVal_TOP_CARD(hv2));
    TEST_ASSERT_EQUAL_INT(HandVal_SECOND_CARD(hv1), HandVal_SECOND_CARD(hv2));
}

static void test_pineapple_kicker_comparison(void)
{
    // Test kicker comparison in pairs
    StdDeck_CardMask hand1 = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask hand2 = create_hand(
        StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

    HandVal hv1 = StdDeck_StdRules_EVAL_N(hand1, 3);
    HandVal hv2 = StdDeck_StdRules_EVAL_N(hand2, 3);

    // Both are pairs of kings, but hand1 has ace kicker vs queen kicker
    TEST_ASSERT_TRUE(hv1 > hv2);
}

static void test_pineapple_maximum_cards(void)
{
    // Test with more than 3 cards (should still work)
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES));

    HandVal hv = StdDeck_StdRules_EVAL_N(hand, 5);
    TEST_ASSERT_NOT_EQUAL(0, hv);
    // Should pick best 5-card hand (trips)
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(hv));
}

static void test_pineapple_basic_functionality(void)
{
    // Test basic three-card poker functionality
    StdDeck_CardMask hand1 = create_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    StdDeck_CardMask hand2 = create_hand(
        StdDeck_Rank_2, StdDeck_Suit_SPADES,
        StdDeck_Rank_3, StdDeck_Suit_HEARTS,
        StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

    HandVal hv1 = StdDeck_StdRules_EVAL_N(hand1, 3);
    HandVal hv2 = StdDeck_StdRules_EVAL_N(hand2, 3);

    // AKQ should beat 234
    TEST_ASSERT_TRUE(hv1 > hv2);
}

int main(void)
{
    UNITY_BEGIN();

    // Basic functionality tests
    RUN_TEST(test_pineapple_rules_constants);
    RUN_TEST(test_pineapple_three_card_evaluation);
    RUN_TEST(test_pineapple_basic_functionality);

    // Hand type tests
    RUN_TEST(test_pineapple_trips);
    RUN_TEST(test_pineapple_straight);
    RUN_TEST(test_pineapple_flush);
    RUN_TEST(test_pineapple_straight_flush);
    RUN_TEST(test_pineapple_high_card);

    // Comparison tests
    RUN_TEST(test_pineapple_hand_comparison);
    RUN_TEST(test_pineapple_kicker_comparison);

    // Advanced scenarios
    RUN_TEST(test_pineapple_with_board);
    RUN_TEST(test_pineapple_wheel_straight);
    RUN_TEST(test_pineapple_different_suits);

    // Edge cases
    RUN_TEST(test_pineapple_edge_cases);
    RUN_TEST(test_pineapple_maximum_cards);

    return UNITY_END();
}
