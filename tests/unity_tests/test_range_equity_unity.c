#include "unity.h"
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static int is_smoke_mode(void)
{
    const char *smoke = getenv("PE_TEST_SMOKE");
    return smoke && *smoke && strcmp(smoke, "0") != 0;
}

// Helper function to create a simple hand mask
static StdDeck_CardMask create_hand(int rank1, int suit1, int rank2, int suit2)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank1, suit1));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank2, suit2));
    return hand;
}

// Test basic PlayerRange structure
static void test_player_range_creation(void)
{
    StdDeck_CardMask hands[3];
    PlayerRange range;
    memset(&range, 0, sizeof(range));

    // Create some hands
    hands[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands[1] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS, StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    hands[2] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS, StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

    // Create range
    range.hand_masks = hands;
    range.weights = NULL;
    range.count = 3;
    range.total_weight = 3.0;

    TEST_ASSERT_EQUAL_PTR(hands, range.hand_masks);
    TEST_ASSERT_EQUAL_INT(3, range.count);
}

// Test basic equity calculation with simple ranges
static void test_basic_equity_calculation(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 2000 : 10000;

    // Create pocket aces vs pocket kings
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS, StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    // Empty board and dead cards
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    // Calculate equity using Monte Carlo
    result = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        5,     // Deal 5 board cards
        true,  // Use Monte Carlo
        iterations,
        0,
        &results);

    TEST_ASSERT_TRUE(result > 0); // Should have valid results
    TEST_ASSERT_EQUAL_INT(2, results.nplayers);
    // Note: nsamples might be different from requested due to early termination or other factors
    TEST_ASSERT_TRUE(results.nsamples > 0);

    // AA should have higher equity than KK preflop
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]);

    // Equity should sum to approximately 1.0 (allowing for ties)
    double total_equity = results.ev[0] + results.ev[1];
    TEST_ASSERT_TRUE(total_equity >= 0.95 && total_equity <= 1.05);
}

// Test equity calculation with board cards
static void test_equity_with_board(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 1000 : 5000;

    // Create AK vs QQ
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS, StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    // Board with Ace (favors AK)
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_RESET(dead_cards);

    // Calculate equity with 2 more cards to come
    result = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        2,    // Deal 2 more board cards
        true, // Use Monte Carlo
        iterations,
        0,
        &results);

    TEST_ASSERT_TRUE(result > 0);

    // AK should now have higher equity with the Ace on board
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]);
}

// Test multiple hands in range
static void test_multiple_hands_range(void)
{
    StdDeck_CardMask hands1[2], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 1000 : 5000;

    // Player 1 has AA and KK
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands1[1] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS, StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    // Player 2 has QQ
    hands2[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES, StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 2;
    ranges[0].total_weight = 2.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        5,
        true,
        iterations,
        0,
        &results);

    TEST_ASSERT_TRUE(result > 0);

    // Player 1 with premium range should have higher equity
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]);
}

// Test error handling - conflicting hands
static void test_conflicting_hands(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    // Both players have the same hand (should conflict)
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        5,
        true,
        1000,
        0,
        &results);

    // Should return 0 for no valid matchups
    TEST_ASSERT_EQUAL_INT(0, result);
}

// Test error handling - empty range
static void test_empty_range(void)
{
    StdDeck_CardMask hands1[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = NULL; // Empty range
    ranges[1].weights = NULL;
    ranges[1].count = 0;
    ranges[1].total_weight = 0.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        5,
        true,
        1000,
        0,
        &results);

    // Should return 0 for empty range
    TEST_ASSERT_EQUAL_INT(0, result);
}

// Test exhaustive vs Monte Carlo consistency
static void test_exhaustive_vs_montecarlo(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results_exhaustive, results_mc;
    int result1, result2;

    // Simple case with board cards to reduce computation
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS, StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    // Board with 4 cards to reduce exhaustive computation
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask_RESET(dead_cards);

    // Exhaustive calculation
    result1 = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        1,     // Only 1 card to come
        false, // Exhaustive
        0,
        0,
        &results_exhaustive);

    // Monte Carlo calculation
    result2 = CalculateEquityForRanges(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        1,
        true, // Monte Carlo
        10000,
        0,
        &results_mc);

    TEST_ASSERT_TRUE(result1 > 0);
    TEST_ASSERT_TRUE(result2 > 0);

    // Results should be reasonably close (within 5%)
    double diff = fabs(results_exhaustive.ev[0] - results_mc.ev[0]);
    TEST_ASSERT_TRUE(diff < 0.05);
}

// Test auto selection function
static void test_auto_selection(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS, StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges_Auto(
        game_holdem,
        ranges,
        2,
        board,
        dead_cards,
        5,
        true,
        5000,
        0,
        &results);

    TEST_ASSERT_TRUE(result > 0);
    TEST_ASSERT_EQUAL_INT(2, results.nplayers);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_player_range_creation);
    RUN_TEST(test_basic_equity_calculation);
    RUN_TEST(test_equity_with_board);
    RUN_TEST(test_multiple_hands_range);
    RUN_TEST(test_conflicting_hands);
    RUN_TEST(test_empty_range);
    RUN_TEST(test_exhaustive_vs_montecarlo);
    RUN_TEST(test_auto_selection);
    return UNITY_END();
}
