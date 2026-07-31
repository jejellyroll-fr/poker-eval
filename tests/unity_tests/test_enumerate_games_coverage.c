/**
 * test_enumerate_games_coverage.c
 *
 * Targeted coverage tests for enumerate.c. Exercises code paths that the
 * existing comprehensive/extended suites do not reach within the 30s CTest
 * budget: result printing helpers, error/edge branches, and a broad sweep of
 * holdem/omaha game variants evaluated on a full board (0 cards left to deal,
 * so enumeration is instantaneous).
 */

#include "unity.h"
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/poker_defs.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static StdDeck_CardMask mk_card(int rank, int suit)
{
    StdDeck_CardMask m;
    StdDeck_CardMask_RESET(m);
    StdDeck_CardMask_SET(m, StdDeck_MAKE_CARD(rank, suit));
    return m;
}

static void set_hand(StdDeck_CardMask *m, int n, int ranks[], int suits[])
{
    StdDeck_CardMask_RESET(*m);
    for (int i = 0; i < n; i++)
        StdDeck_CardMask_SET(*m, StdDeck_MAKE_CARD(ranks[i], suits[i]));
}

/* Cover enumResultPrint / enumResultPrintTerse / PrintOrdering. */
static void test_result_print_helpers(void)
{
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int err;

    int r0[2] = {StdDeck_Rank_ACE, StdDeck_Rank_ACE};
    int s0[2] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS};
    int r1[2] = {StdDeck_Rank_KING, StdDeck_Rank_KING};
    int s1[2] = {StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    set_hand(&pockets[0], 2, r0, s0);
    set_hand(&pockets[1], 2, r1, s1);

    board = mk_card(StdDeck_Rank_2, StdDeck_Suit_SPADES);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    /* Cleared, not allocated: the enumeration below starts with
     * enumResultClear(), which would drop an ordering allocated here. */
    enumResultClear(&result);

    err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* These call the previously-uncovered print paths (incl. PrintOrdering). */
    enumResultPrint(&result, pockets, board);
    enumResultPrintTerse(&result, pockets, board);

    enumResultFree(&result);
}

/* Cover enumGameParams error branch (invalid game) and enumExhaustive error
 * return for an out-of-range game type. */
static void test_invalid_game_params(void)
{
    enum_gameparams_t *params;

    params = enumGameParams((enum_game_t)999);
    TEST_ASSERT_NULL(params);

    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    int r0[2] = {StdDeck_Rank_ACE, StdDeck_Rank_ACE};
    int s0[2] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS};
    int r1[2] = {StdDeck_Rank_KING, StdDeck_Rank_KING};
    int s1[2] = {StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    set_hand(&pockets[0], 2, r0, s0);
    set_hand(&pockets[1], 2, r1, s1);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    memset(&result, 0, sizeof(result));
    int err = enumExhaustive((enum_game_t)999, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_NOT_EQUAL(0, err);
}

/* Cover enumResultAlloc edge cases. */
static void test_result_alloc_edge(void)
{
    enum_result_t result;
    int err;

    /* nplayers = 1 (below minimum) - implementation clamps/allocates. */
    memset(&result, 0, sizeof(result));
    err = enumResultAlloc(&result, 1, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);
    enumResultFree(&result);

    /* ordering mode lo. */
    err = enumResultAlloc(&result, 2, enum_ordering_mode_lo);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    enumResultFree(&result);
}

/* Sweep holdem-family + omaha-family variants on a full board (instant). */
static void test_game_variant_sweep(void)
{
    enum_game_t holdem_games[] = {game_holdem, game_holdem8};
    enum_game_t omaha_games[] = {game_omaha, game_omaha5, game_omaha6,
                                 game_omaha8, game_omaha85, game_omaha86};

    /* Holdem family: 2-card pockets. */
    StdDeck_CardMask hp[2], board, dead;
    int hr0[2] = {StdDeck_Rank_ACE, StdDeck_Rank_ACE};
    int hs0[2] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS};
    int hr1[2] = {StdDeck_Rank_KING, StdDeck_Rank_KING};
    int hs1[2] = {StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    set_hand(&hp[0], 2, hr0, hs0);
    set_hand(&hp[1], 2, hr1, hs1);
    board = mk_card(StdDeck_Rank_2, StdDeck_Suit_SPADES);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS));

    for (size_t i = 0; i < sizeof(holdem_games) / sizeof(holdem_games[0]); i++) {
        StdDeck_CardMask_RESET(dead);
        StdDeck_CardMask_OR(dead, dead, hp[0]);
        StdDeck_CardMask_OR(dead, dead, hp[1]);
        StdDeck_CardMask_OR(dead, dead, board);
        enum_result_t res;
        memset(&res, 0, sizeof(res));
        int err = enumExhaustive(holdem_games[i], hp, board, dead, 2, 5, 0, &res);
        TEST_ASSERT_EQUAL_INT(0, err);
        TEST_ASSERT_TRUE(res.nsamples > 0);
        enumResultFree(&res);
    }

    /* Omaha family: 4-card pockets. Use a board that does not collide with
     * the eight pocket cards (As Kh Qc Jd / 2s 3h 4c 5d). */
    StdDeck_CardMask op[2];
    int or0[4] = {StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN, StdDeck_Rank_JACK};
    int os0[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    int or1[4] = {StdDeck_Rank_2, StdDeck_Rank_3, StdDeck_Rank_4, StdDeck_Rank_5};
    int os1[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    set_hand(&op[0], 4, or0, os0);
    set_hand(&op[1], 4, or1, os1);

    StdDeck_CardMask oboard = mk_card(StdDeck_Rank_6, StdDeck_Suit_SPADES);
    StdDeck_CardMask_SET(oboard, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(oboard, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(oboard, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(oboard, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

    for (size_t i = 0; i < sizeof(omaha_games) / sizeof(omaha_games[0]); i++) {
        StdDeck_CardMask_RESET(dead);
        StdDeck_CardMask_OR(dead, dead, op[0]);
        StdDeck_CardMask_OR(dead, dead, op[1]);
        StdDeck_CardMask_OR(dead, dead, oboard);
        enum_result_t res;
        memset(&res, 0, sizeof(res));
        int err = enumExhaustive(omaha_games[i], op, oboard, dead, 2, 5, 0, &res);
        TEST_ASSERT_EQUAL_INT(0, err);
        TEST_ASSERT_TRUE(res.nsamples > 0);
        enumResultFree(&res);
    }
}

/* Monte Carlo sweep over a few more variants (fast, modest sample size). */
static void test_sample_variant_sweep(void)
{
    enum_game_t games[] = {game_omaha8, game_holdem8, game_omaha};

    StdDeck_CardMask pockets[2], board, dead;
    int r0[4] = {StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN, StdDeck_Rank_JACK};
    int s0[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    int r1[4] = {StdDeck_Rank_2, StdDeck_Rank_3, StdDeck_Rank_4, StdDeck_Rank_5};
    int s1[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_CLUBS, StdDeck_Suit_DIAMONDS};
    set_hand(&pockets[0], 4, r0, s0);
    set_hand(&pockets[1], 4, r1, s1);

    board = mk_card(StdDeck_Rank_9, StdDeck_Suit_SPADES);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    StdDeck_CardMask_OR(dead, dead, board);

    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++) {
        enum_result_t res;
        memset(&res, 0, sizeof(res));
        int err = enumSample(games[i], pockets, board, dead, 2, 3, 2000, 0, &res);
        TEST_ASSERT_EQUAL_INT(0, err);
        TEST_ASSERT_EQUAL_INT(2000, res.nsamples);
        enumResultFree(&res);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_result_print_helpers);
    RUN_TEST(test_invalid_game_params);
    RUN_TEST(test_result_alloc_edge);
    RUN_TEST(test_game_variant_sweep);
    RUN_TEST(test_sample_variant_sweep);
    return UNITY_END();
}
