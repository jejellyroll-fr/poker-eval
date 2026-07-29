/*
 * test_range_equity_coverage.c - Coverage tests for RangeEquity.c
 *
 * Tests CalculateEquityForRanges with various scenarios:
 * - 2-player and 3-player ranges
 * - Empty/invalid ranges
 * - Board conflict handling
 * - Monte Carlo vs exhaustive
 * - Omaha ranges
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/RangeEquity.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Helper: create a hand mask from 2 cards */
static StdDeck_CardMask make_hand2(int r1, int s1, int r2, int s2) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    add_card(&hand, r1, s1);
    add_card(&hand, r2, s2);
    return hand;
}

/* ===== Basic 2-player range equity ===== */

void test_range_equity_basic_2p(void) {
    /* Simple AA vs KK range equity */
    StdDeck_CardMask hands_p1[3];
    StdDeck_CardMask hands_p2[3];

    /* AA combos */
    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands_p1[1] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    hands_p1[2] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    /* KK combos */
    hands_p2[0] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                             StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands_p2[1] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    hands_p2[2] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 3;
    ranges[0].total_weight = 3.0;

    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 3;
    ranges[1].total_weight = 3.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Full board so enumeration is fast */
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_holdem, ranges, 2,
                                             board, dead, 5, false, 0, 0, &result);

    TEST_ASSERT_TRUE(matchups > 0);
    /* AA should dominate KK */
    TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    /* Equities should sum to approximately 1 */
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, result.ev[0] + result.ev[1]);
}

/* ===== Range equity with Monte Carlo ===== */

void test_range_equity_montecarlo(void) {
    StdDeck_CardMask hands_p1[2];
    StdDeck_CardMask hands_p2[2];

    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands_p1[1] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    hands_p2[0] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                             StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands_p2[1] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 2;
    ranges[0].total_weight = 2.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 2;
    ranges[1].total_weight = 2.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Flop */
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_holdem, ranges, 2,
                                             board, dead, 3, true, 1000, 0, &result);

    TEST_ASSERT_TRUE(matchups > 0);
    /* AA should still beat KK on this board */
    TEST_ASSERT_TRUE(result.ev[0] > 0.5);
}

/* ===== Error: invalid number of players ===== */

void test_range_equity_invalid_players(void) {
    enum_result_t result;
    enumResultClear(&result);

    int ret = CalculateEquityForRanges(game_holdem, NULL, 0,
                                        (StdDeck_CardMask){{0}}, (StdDeck_CardMask){{0}},
                                        0, false, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_range_equity_null_result(void) {
    PlayerRange ranges[2];
    StdDeck_CardMask hands[1];
    hands[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                          StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    ranges[0].hand_masks = hands;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1] = ranges[0];

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    int ret = CalculateEquityForRanges(game_holdem, ranges, 2,
                                        board, dead, 0, false, 0, 0, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ===== Empty range ===== */

void test_range_equity_empty_range(void) {
    StdDeck_CardMask hands_p1[1];
    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;

    /* Empty range for player 2 */
    ranges[1].hand_masks = NULL;
    ranges[1].weights = NULL;
    ranges[1].count = 0;
    ranges[1].total_weight = 0.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enum_result_t result;
    enumResultClear(&result);

    int ret = CalculateEquityForRanges(game_holdem, ranges, 2,
                                        board, dead, 0, false, 0, 0, &result);
    /* Should return 0 matchups for empty range */
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/* ===== All hands conflict with board ===== */

void test_range_equity_all_hands_conflict(void) {
    StdDeck_CardMask hands_p1[1];
    StdDeck_CardMask hands_p2[1];

    /* Use AcKc for player 1 */
    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    /* Use 7h2d for player 2 */
    hands_p2[0] = make_hand2(StdDeck_Rank_7, StdDeck_Suit_HEARTS,
                             StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Board contains Ac - conflicts with player 1's hand */
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int ret = CalculateEquityForRanges(game_holdem, ranges, 2,
                                        board, dead, 5, false, 0, 0, &result);
    /* Player 1's only hand conflicts with board -> 0 valid hands -> return 0 */
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/* ===== Two players with overlapping hands ===== */

void test_range_equity_overlap(void) {
    StdDeck_CardMask hands_p1[2];
    StdDeck_CardMask hands_p2[2];

    /* Player 1: AsAh, AcAd */
    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands_p1[1] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    /* Player 2: AsKs, KhKd - AsKs overlaps with P1's AsAh */
    hands_p2[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    hands_p2[1] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_HEARTS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 2;
    ranges[0].total_weight = 2.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 2;
    ranges[1].total_weight = 2.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_holdem, ranges, 2,
                                             board, dead, 5, false, 0, 0, &result);

    /* Some matchups should be skipped due to overlap */
    TEST_ASSERT_TRUE(matchups > 0);
    /* Not all 2*2=4 matchups should be valid (some overlap) */
    TEST_ASSERT_TRUE(matchups < 4);
}

/* ===== 3-player range equity ===== */

void test_range_equity_3p(void) {
    StdDeck_CardMask hands_p1[1], hands_p2[1], hands_p3[1];

    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands_p2[0] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    hands_p3[0] = make_hand2(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES,
                             StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    PlayerRange ranges[3];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;
    ranges[2].hand_masks = hands_p3;
    ranges[2].weights = NULL;
    ranges[2].count = 1;
    ranges[2].total_weight = 1.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_holdem, ranges, 3,
                                             board, dead, 5, false, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(1, matchups);
    /* AA should have highest equity */
    TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);
    TEST_ASSERT_TRUE(result.ev[0] > result.ev[2]);
}

/* ===== Range equity with dead cards ===== */

void test_range_equity_with_dead(void) {
    StdDeck_CardMask hands_p1[2];
    StdDeck_CardMask hands_p2[2];

    hands_p1[0] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                             StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    hands_p1[1] = make_hand2(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);

    hands_p2[0] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_SPADES,
                             StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    hands_p2[1] = make_hand2(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                             StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 2;
    ranges[0].total_weight = 2.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 2;
    ranges[1].total_weight = 2.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    /* Kill AcAd by adding Ac to dead */
    add_card(&dead, StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_holdem, ranges, 2,
                                             board, dead, 5, false, 0, 0, &result);

    /* Only AsAh should be valid for P1 (AcAd conflicts with dead Ac) */
    TEST_ASSERT_TRUE(matchups > 0);
    /* Matchups should be less than 2*2 because of filtering */
    TEST_ASSERT_TRUE(matchups <= 2);
}

/* ===== Omaha range equity ===== */

void test_range_equity_omaha(void) {
    StdDeck_CardMask hands_p1[1], hands_p2[1];

    /* Player 1: AcAdKcKd */
    StdDeck_CardMask_RESET(hands_p1[0]);
    add_card(&hands_p1[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&hands_p1[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&hands_p1[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&hands_p1[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

    /* Player 2: QsQhJsJh */
    StdDeck_CardMask_RESET(hands_p2[0]);
    add_card(&hands_p2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&hands_p2[0], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&hands_p2[0], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&hands_p2[0], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

    PlayerRange ranges[2];
    ranges[0].hand_masks = hands_p1;
    ranges[0].weights = NULL;
    ranges[0].count = 1;
    ranges[0].total_weight = 1.0;
    ranges[1].hand_masks = hands_p2;
    ranges[1].weights = NULL;
    ranges[1].count = 1;
    ranges[1].total_weight = 1.0;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    enum_result_t result;
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(game_omaha, ranges, 2,
                                             board, dead, 5, false, 0, 0, &result);

    TEST_ASSERT_EQUAL_INT(1, matchups);
    /* Both equities should be valid */
    TEST_ASSERT_TRUE(result.ev[0] >= 0.0);
    TEST_ASSERT_TRUE(result.ev[1] >= 0.0);
}

int main(void) {
    UNITY_BEGIN();

    /* Basic tests */
    RUN_TEST(test_range_equity_basic_2p);
    RUN_TEST(test_range_equity_montecarlo);

    /* Error handling */
    RUN_TEST(test_range_equity_invalid_players);
    RUN_TEST(test_range_equity_null_result);
    RUN_TEST(test_range_equity_empty_range);
    RUN_TEST(test_range_equity_all_hands_conflict);

    /* Overlap and conflict */
    RUN_TEST(test_range_equity_overlap);

    /* Multiway */
    RUN_TEST(test_range_equity_3p);

    /* Dead cards */
    RUN_TEST(test_range_equity_with_dead);

    /* Omaha */
    RUN_TEST(test_range_equity_omaha);

    return UNITY_END();
}
