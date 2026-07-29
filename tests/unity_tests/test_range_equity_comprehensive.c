/**
 * test_range_equity_comprehensive.c
 * 
 * Comprehensive tests for RangeEquity.c and related distribution code.
 * Covers edge cases, error handling, and various range configurations.
 */

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

/* Helper to create a hand mask */
static StdDeck_CardMask create_hand(int rank1, int suit1, int rank2, int suit2)
{
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank1, suit1));
    StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(rank2, suit2));
    return hand;
}

/* Helper to create a 4-card Omaha hand */
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

/* Test: Single hand vs single hand (baseline) */
static void test_single_vs_single(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 1000 : 5000;

    /* AA vs KK */
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES, 
                            StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS, 
                            StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

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
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]); /* AA > KK */
    TEST_ASSERT_TRUE(results.ev[0] > 0.7); /* AA should have >70% equity */
}

/* Test: Weighted ranges */
static void test_weighted_ranges(void)
{
    StdDeck_CardMask hands1[3];
    double weights1[3] = {1.0, 0.5, 0.25}; /* Different weights */
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 1000 : 3000;

    /* Create range with different weighted hands */
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands1[1] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    hands1[2] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                            StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    StdDeck_CardMask hands2[1];
    hands2[0] = create_hand(StdDeck_Rank_2, StdDeck_Suit_SPADES,
                            StdDeck_Rank_2, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = weights1;
    ranges[0].count = 3;
    ranges[0].total_weight = 1.75;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    /* Weighted range should have reasonable equity */
    TEST_ASSERT_TRUE(results.ev[0] >= 0.0 && results.ev[0] <= 1.0);
    TEST_ASSERT_TRUE(results.ev[1] >= 0.0 && results.ev[1] <= 1.0);
}

/* Test: Large ranges */
static void test_large_range(void)
{
    #define LARGE_RANGE_SIZE 20
    StdDeck_CardMask hands1[LARGE_RANGE_SIZE];
    StdDeck_CardMask hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 500 : 2000;
    int hand_count = 0;

    /* Build a large range of pocket pairs */
    for (int rank = StdDeck_Rank_ACE; rank >= StdDeck_Rank_2 && hand_count < LARGE_RANGE_SIZE; rank--) {
        if (hand_count >= LARGE_RANGE_SIZE - 1) break;
        
        /* Create pocket pair with first available suits */
        hands1[hand_count] = create_hand(rank, StdDeck_Suit_SPADES,
                                         rank, StdDeck_Suit_HEARTS);
        hand_count++;
        
        if (hand_count < LARGE_RANGE_SIZE) {
            hands1[hand_count] = create_hand(rank, StdDeck_Suit_CLUBS,
                                             rank, StdDeck_Suit_DIAMONDS);
            hand_count++;
        }
    }

    hands2[0] = create_hand(StdDeck_Rank_7, StdDeck_Suit_SPADES,
                            StdDeck_Rank_2, StdDeck_Suit_CLUBS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = hand_count;
    ranges[0].total_weight = (double)hand_count;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    /* Range of pairs should crush 72o */
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]);
    #undef LARGE_RANGE_SIZE
}

/* Test: Board texture effects */
static void test_board_effects(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 1000 : 3000;

    /* Set vs flush draw */
    hands1[0] = create_hand(StdDeck_Rank_7, StdDeck_Suit_SPADES,
                            StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    /* Flop with flush draw: 7c 2c 9h */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 3,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    /* Set should be ahead but flush draw has outs */
    TEST_ASSERT_TRUE(results.ev[0] > 0.5);
    TEST_ASSERT_TRUE(results.ev[1] > 0.2); /* Flush draw has ~35% equity */
}

/* Test: Omaha game type */
static void test_omaha_ranges(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 500 : 2000;

    /* Create 4-card Omaha hands */
    hands1[0] = create_omaha_hand(
        StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
        StdDeck_Rank_ACE, StdDeck_Suit_HEARTS,
        StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
        StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    hands2[0] = create_omaha_hand(
        StdDeck_Rank_2, StdDeck_Suit_SPADES,
        StdDeck_Rank_3, StdDeck_Suit_HEARTS,
        StdDeck_Rank_4, StdDeck_Suit_CLUBS,
        StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);

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
        game_omaha, ranges, 2, board, dead_cards, 5,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    /* Both hands should have some equity */
    TEST_ASSERT_TRUE(results.ev[0] > 0.0);
    TEST_ASSERT_TRUE(results.ev[1] > 0.0);
}

/* Test: Three player range equity */
static void test_three_player_ranges(void)
{
    StdDeck_CardMask hands1[1], hands2[1], hands3[1];
    PlayerRange ranges[3];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 500 : 2000;

    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    hands3[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
                            StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;
    
    ranges[2].hand_masks = hands3;
    ranges[2].weights = NULL;
    ranges[2].count = 1;
    ranges[2].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 3, board, dead_cards, 5,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    TEST_ASSERT_EQUAL_INT(3, results.nplayers);
    
    /* AA > KK > QQ in equity */
    TEST_ASSERT_TRUE(results.ev[0] > results.ev[1]);
    TEST_ASSERT_TRUE(results.ev[1] > results.ev[2]);
}

/* Test: Dead cards affecting range */
static void test_dead_cards(void)
{
    StdDeck_CardMask hands1[2], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 500 : 2000;

    /* Range includes both AA combinations */
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands1[1] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    hands2[0] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                            StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 2;
    ranges[0].total_weight = 2.0;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask_RESET(board);
    
    /* Mark Ace of Hearts as dead - should eliminate hands1[0] */
    StdDeck_CardMask_RESET(dead_cards);
    StdDeck_CardMask_SET(dead_cards, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, iterations, 0, &results);

    /* Should still work with one hand eliminated */
    TEST_ASSERT_TRUE(result > 0);
}

/* Test: Error case - null ranges */
static void test_null_range_error(void)
{
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    /* Both ranges are null/empty */
    ranges[0].hand_masks = NULL;
    ranges[0].weights = NULL;
    ranges[0].count = 0;
    ranges[0].total_weight = 0.0;
    
    ranges[1].hand_masks = NULL;
    ranges[1].weights = NULL;
    ranges[1].count = 0;
    ranges[1].total_weight = 0.0;

    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, 1000, 0, &results);

    /* Should fail gracefully */
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* Test: Mixed exhaustive/MC decision making */
static void test_auto_mode_selection(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    /* Board with 4 cards - small search space */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));

    StdDeck_CardMask_RESET(dead_cards);

    /* Use auto mode */
    result = CalculateEquityForRanges_Auto(
        game_holdem, ranges, 2, board, dead_cards, 4,
        true, 5000, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
}

/* Test: Very small iteration count */
static void test_minimal_iterations(void)
{
    StdDeck_CardMask hands1[1], hands2[1];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;

    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands2[0] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

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

    /* Very few iterations - may be reduced by auto-optimization */
    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 5,
        true, 10, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    /* nsamples may be less than requested if using exhaustive mode */
    TEST_ASSERT_TRUE(results.nsamples >= 1 && results.nsamples <= 10);
}

/* Test: Range vs range with board cards */
static void test_range_vs_range_with_board(void)
{
    StdDeck_CardMask hands1[3], hands2[3];
    PlayerRange ranges[2];
    memset(ranges, 0, sizeof(ranges));
    StdDeck_CardMask board, dead_cards;
    enum_result_t results;
    int result;
    int iterations = is_smoke_mode() ? 500 : 1500;

    /* Create multiple hands for each player */
    hands1[0] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                            StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands1[1] = create_hand(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    hands1[2] = create_hand(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                            StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    hands2[0] = create_hand(StdDeck_Rank_JACK, StdDeck_Suit_SPADES,
                            StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    hands2[1] = create_hand(StdDeck_Rank_TEN, StdDeck_Suit_CLUBS,
                            StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS);
    hands2[2] = create_hand(StdDeck_Rank_9, StdDeck_Suit_SPADES,
                            StdDeck_Rank_9, StdDeck_Suit_HEARTS);

    ranges[0].hand_masks = hands1;
    ranges[0].weights = NULL;
    ranges[0].count = 3;
    ranges[0].total_weight = 3.0;
    
    ranges[1].hand_masks = hands2;
    ranges[1].weights = NULL;
    ranges[1].count = 3;
    ranges[1].total_weight = 3.0;

    /* Flop that doesn't strongly favor either range */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_RESET(dead_cards);

    result = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead_cards, 3,
        true, iterations, 0, &results);

    TEST_ASSERT_TRUE(result > 0);
    
    /* Pairs should have advantage over unpaired hands */
    TEST_ASSERT_TRUE(results.ev[1] > results.ev[0]);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_single_vs_single);
    RUN_TEST(test_weighted_ranges);
    RUN_TEST(test_large_range);
    RUN_TEST(test_board_effects);
    RUN_TEST(test_omaha_ranges);
    RUN_TEST(test_three_player_ranges);
    RUN_TEST(test_dead_cards);
    RUN_TEST(test_null_range_error);
    RUN_TEST(test_auto_mode_selection);
    RUN_TEST(test_minimal_iterations);
    RUN_TEST(test_range_vs_range_with_board);
    
    return UNITY_END();
}
