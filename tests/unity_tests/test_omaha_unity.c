#include "unity.h"
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/games/eval_omaha.h>

void setUp(void) {}
void tearDown(void) {}

// Helper function to create Omaha hand (4 cards)
static StdDeck_CardMask create_omaha_hand(int r1, int s1, int r2, int s2, int r3, int s3, int r4, int s4)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r1, s1));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r2, s2));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r3, s3));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(r4, s4));
    return hand;
}

// Helper function to create board (5 cards)
static StdDeck_CardMask create_board(int r1, int s1, int r2, int s2, int r3, int s3, int r4, int s4, int r5, int s5)
{
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(r1, s1));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(r2, s2));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(r3, s3));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(r4, s4));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(r5, s5));
    return board;
}

// Test basic Omaha hand validation
static void test_omaha_hand_validation(void)
{
    StdDeck_CardMask hand;

    // Valid Omaha hand (4 cards)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    TEST_ASSERT_EQUAL_INT(4, StdDeck_numCards(hand));

    // Verify all cards are present
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES)));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS)));
    TEST_ASSERT_TRUE(StdDeck_CardMask_CARD_IS_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_CLUBS)));
}

// Test Omaha evaluation with nuts
static void test_omaha_nuts_evaluation(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ A♥ K♠ Q♠ (nut flush draw + pair of aces)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

    // Board: J♠ T♠ 9♠ 2♥ 3♦ (gives royal flush)
    board = create_board(
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES,
        StdDeck_Rank_TEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_2, StdDeck_Suit_HEARTS,
        StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

    // In Omaha, must use exactly 2 from hand and 3 from board
    // This should make a royal flush: A♠ K♠ Q♠ J♠ T♠
    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be a straight flush (royal flush)
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

// Test Omaha full house
static void test_omaha_full_house(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ A♥ 9♠ 8♥ (pair of aces)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    // Board: A♦ 9♦ 9♣ 7♥ 5♣ (gives full house Aces over Nines)
    board = create_board(
        StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_9, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_9, StdDeck_Suit_CLUBS,
        StdDeck_Rank_7, StdDeck_Suit_HEARTS,
        StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be a full house
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FULLHOUSE, HandVal_HANDTYPE(hv));
}

// Test Omaha straight
static void test_omaha_straight(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ 2♥ 7♠ 8♥ (wheel draw)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_2, StdDeck_Suit_HEARTS,
        StdDeck_Rank_7, StdDeck_Suit_SPADES,
        StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    // Board: 3♦ 4♦ 5♠ K♥ Q♣ (gives wheel straight A-2-3-4-5)
    board = create_board(
        StdDeck_Rank_3, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_4, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_5, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be a straight
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STRAIGHT, HandVal_HANDTYPE(hv));
}

// Test Omaha flush
static void test_omaha_flush(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ K♠ 7♥ 8♥ (spade flush draw)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_7, StdDeck_Suit_HEARTS,
        StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    // Board: Q♠ J♠ 9♠ 2♥ 3♦ (gives nut flush)
    board = create_board(
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_2, StdDeck_Suit_HEARTS,
        StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be a flush
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_FLUSH, HandVal_HANDTYPE(hv));
}

// Test Omaha two pair
static void test_omaha_two_pair(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ A♥ K♠ Q♥ (pair of aces)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    // Board: K♦ Q♦ J♠ 9♥ 7♣ (gives two pair Aces and Kings)
    board = create_board(
        StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES,
        StdDeck_Rank_9, StdDeck_Suit_HEARTS,
        StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be two pair
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TWOPAIR, HandVal_HANDTYPE(hv));
}

// Test Omaha one pair
static void test_omaha_one_pair(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ K♥ Q♠ J♥ (high cards)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    // Board: A♦ 9♦ 7♠ 5♥ 3♣ (gives pair of aces)
    board = create_board(
        StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_9, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_7, StdDeck_Suit_SPADES,
        StdDeck_Rank_5, StdDeck_Suit_HEARTS,
        StdDeck_Rank_3, StdDeck_Suit_CLUBS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be one pair
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_ONEPAIR, HandVal_HANDTYPE(hv));
}

// Test Omaha high card
static void test_omaha_high_card(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand: A♠ K♥ 7♠ 5♥ (high cards, no pairs)
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
        StdDeck_Rank_7, StdDeck_Suit_SPADES,
        StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    // Board: Q♦ J♦ 9♠ 8♥ 3♣ (no pairs, no straights, no flushes)
    board = create_board(
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_8, StdDeck_Suit_HEARTS,
        StdDeck_Rank_3, StdDeck_Suit_CLUBS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be high card
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_NOPAIR, HandVal_HANDTYPE(hv));
}

// Test Omaha hand comparison
static void test_omaha_hand_comparison(void)
{
    StdDeck_CardMask hand1, hand2, board;
    HandVal hv1, hv2;
    int result1, result2;

    // Hand 1: A♠ A♥ K♠ K♥ (two pair)
    hand1 = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    // Hand 2: Q♠ Q♥ J♠ J♥ (two pair, lower)
    hand2 = create_omaha_hand(
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS,
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES,
        StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    // Board: A♦ Q♦ 9♠ 7♥ 5♣ (hand1 makes trips of Aces, hand2 makes trips of Queens)
    board = create_board(
        StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_7, StdDeck_Suit_HEARTS,
        StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    result1 = StdDeck_OmahaHi_EVAL(hand1, board, &hv1);
    result2 = StdDeck_OmahaHi_EVAL(hand2, board, &hv2);

    TEST_ASSERT_EQUAL_INT(0, result1);
    TEST_ASSERT_EQUAL_INT(0, result2);

    // Both should be trips
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(hv1));
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_TRIPS, HandVal_HANDTYPE(hv2));

    // Hand 1 should be better (trips of Aces vs trips of Queens)
    TEST_ASSERT_TRUE(hv1 > hv2);
}

// Test Omaha with exactly 2 hole cards rule
static void test_omaha_two_card_rule(void)
{
    StdDeck_CardMask hand, board;
    HandVal hv;
    int result;

    // Hand with 4 spades: A♠ K♠ Q♠ J♠
    hand = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_KING, StdDeck_Suit_SPADES,
        StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_JACK, StdDeck_Suit_SPADES);

    // Board with 3 spades: T♠ 9♠ 8♠ 7♥ 6♦
    board = create_board(
        StdDeck_Rank_TEN, StdDeck_Suit_SPADES,
        StdDeck_Rank_9, StdDeck_Suit_SPADES,
        StdDeck_Rank_8, StdDeck_Suit_SPADES,
        StdDeck_Rank_7, StdDeck_Suit_HEARTS,
        StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);

    result = StdDeck_OmahaHi_EVAL(hand, board, &hv);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should make a straight flush (using exactly 2 from hand, 3 from board)
    // Best combination: A♠ K♠ from hand + T♠ 9♠ 8♠ from board = royal flush
    // Or Q♠ J♠ from hand + T♠ 9♠ 8♠ from board = straight flush
    TEST_ASSERT_EQUAL_INT(StdRules_HandType_STFLUSH, HandVal_HANDTYPE(hv));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_omaha_hand_validation);
    RUN_TEST(test_omaha_nuts_evaluation);
    RUN_TEST(test_omaha_full_house);
    RUN_TEST(test_omaha_straight);
    RUN_TEST(test_omaha_flush);
    RUN_TEST(test_omaha_two_pair);
    RUN_TEST(test_omaha_one_pair);
    RUN_TEST(test_omaha_high_card);
    RUN_TEST(test_omaha_hand_comparison);
    RUN_TEST(test_omaha_two_card_rule);
    return UNITY_END();
}
