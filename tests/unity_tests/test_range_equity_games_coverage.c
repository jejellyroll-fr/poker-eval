/**
 * test_range_equity_games_coverage.c
 *
 * Targeted coverage tests for RangeEquity.c. The existing suites exercise the
 * main CalculateEquityForRanges path but leave the debug/stats logging and
 * several error branches uncovered. We activate the debug env vars and probe
 * the error paths to lift coverage.
 */

#include "unity.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void build_pair_range(StdDeck_CardMask *hands, int rank, int *count)
{
    *count = 0;
    for (int s1 = 0; s1 < 4; s1++)
        for (int s2 = s1 + 1; s2 < 4; s2++) {
            StdDeck_CardMask_RESET(hands[*count]);
            StdDeck_CardMask_SET(hands[*count], StdDeck_MAKE_CARD(rank, s1));
            StdDeck_CardMask_SET(hands[*count], StdDeck_MAKE_CARD(rank, s2));
            (*count)++;
        }
}

/* Main path with debug/stats logging enabled via env vars. */
static void test_main_path_with_debug(void)
{
    setenv("POKER_DEBUG_RANGE", "1", 1);
    setenv("PE_RANGE_COMBO_TRACE", "1", 1);

    StdDeck_CardMask aa[6], kk[6];
    int na, nk;
    build_pair_range(aa, StdDeck_Rank_ACE, &na);
    build_pair_range(kk, StdDeck_Rank_KING, &nk);

    PlayerRange ranges[2];
    ranges[0].count = na;
    ranges[0].hand_masks = aa;
    ranges[0].weights = NULL;
    ranges[0].total_weight = na;
    ranges[1].count = nk;
    ranges[1].hand_masks = kk;
    ranges[1].weights = NULL;
    ranges[1].total_weight = nk;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enum_result_t result;
    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which would drop an ordering allocated here. */
    enumResultClear(&result);

    int matchups = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead, 5, 1, 3000, 0, &result);
    TEST_ASSERT_TRUE(matchups > 0);

    double sum = result.ev[0] + result.ev[1];
    TEST_ASSERT_TRUE(sum >= 0.95 && sum <= 1.05);

    enumResultFree(&result);
}

/* Error paths. */
static void test_error_paths(void)
{
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    enum_result_t result;
    memset(&result, 0, sizeof(result));

    /* NULL ranges. */
    int ret = CalculateEquityForRanges(
        game_holdem, NULL, 0, board, dead, 5, 1, 100, 0, &result);
    TEST_ASSERT_TRUE(ret < 0);

    /* Zero players. */
    ret = CalculateEquityForRanges(
        game_holdem, NULL, 0, board, dead, 5, 1, 100, 0, &result);
    TEST_ASSERT_TRUE(ret < 0);
}

/* Weighted range path (exercises weight summation branches). */
static void test_weighted_range(void)
{
    StdDeck_CardMask aa[6], kk[6];
    int na, nk;
    build_pair_range(aa, StdDeck_Rank_ACE, &na);
    build_pair_range(kk, StdDeck_Rank_KING, &nk);

    double wa[6], wk[6];
    for (int i = 0; i < na; i++) wa[i] = 1.0;
    for (int i = 0; i < nk; i++) wk[i] = 0.5;

    PlayerRange ranges[2];
    ranges[0].count = na;
    ranges[0].hand_masks = aa;
    ranges[0].weights = wa;
    ranges[0].total_weight = na;
    ranges[1].count = nk;
    ranges[1].hand_masks = kk;
    ranges[1].weights = wk;
    ranges[1].total_weight = nk * 0.5;

    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enum_result_t result;
    enumResultAlloc(&result, 2, enum_ordering_mode_none);

    int matchups = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead, 5, 1, 2000, 0, &result);
    TEST_ASSERT_TRUE(matchups > 0);

    enumResultFree(&result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_main_path_with_debug);
    RUN_TEST(test_error_paths);
    RUN_TEST(test_weighted_range);
    return UNITY_END();
}
