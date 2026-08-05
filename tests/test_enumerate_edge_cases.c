/*
 * test_enumerate_edge_cases.c - Edge case tests for enumerate.c
 *
 * Covers rarely-tested game types, ordering modes, print functions,
 * and boundary conditions to improve code coverage.
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/enumord.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Helper: build dead mask from all pockets + board */
static void build_dead(StdDeck_CardMask *dead, StdDeck_CardMask pockets[],
                       int npockets, StdDeck_CardMask board) {
    StdDeck_CardMask_RESET(*dead);
    for (int i = 0; i < npockets; i++)
        StdDeck_CardMask_OR(*dead, *dead, pockets[i]);
    StdDeck_CardMask_OR(*dead, *dead, board);
}

/* ===== enumGameParams tests ===== */

void test_enumGameParams_holdem(void) {
    enum_gameparams_t *gp = enumGameParams(game_holdem);
    TEST_ASSERT_NOT_NULL(gp);
    TEST_ASSERT_EQUAL_INT(game_holdem, gp->game);
    TEST_ASSERT_EQUAL_INT(2, gp->minpocket);
    TEST_ASSERT_EQUAL_INT(2, gp->maxpocket);
    TEST_ASSERT_EQUAL_INT(5, gp->maxboard);
    TEST_ASSERT_TRUE(gp->hashipot);
}

void test_enumGameParams_omaha8(void) {
    enum_gameparams_t *gp = enumGameParams(game_omaha8);
    TEST_ASSERT_NOT_NULL(gp);
    TEST_ASSERT_EQUAL_INT(game_omaha8, gp->game);
    TEST_ASSERT_TRUE(gp->haslopot);
    TEST_ASSERT_TRUE(gp->hashipot);
}

void test_enumGameParams_razz(void) {
    enum_gameparams_t *gp = enumGameParams(game_razz);
    TEST_ASSERT_NOT_NULL(gp);
    TEST_ASSERT_TRUE(gp->haslopot);
    TEST_ASSERT_FALSE(gp->hashipot);
}

void test_enumGameParams_invalid(void) {
    enum_gameparams_t *gp = enumGameParams(game_NUMGAMES);
    TEST_ASSERT_NULL(gp);
    gp = enumGameParams((enum_game_t)-1);
    TEST_ASSERT_NULL(gp);
}

void test_enumGameParams_all_games(void) {
    /* Iterate through all valid game types */
    for (int g = 0; g < game_NUMGAMES; g++) {
        enum_gameparams_t *gp = enumGameParams((enum_game_t)g);
        TEST_ASSERT_NOT_NULL(gp);
        TEST_ASSERT_EQUAL_INT(g, gp->game);
        TEST_ASSERT_NOT_NULL(gp->name);
        TEST_ASSERT_TRUE(strlen(gp->name) > 0);
    }
}

/* ===== enumResultAlloc/Free/Clear tests ===== */

void test_enumResultAlloc_hi(void) {
    enum_result_t result;
    enumResultClear(&result);
    int err = enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hi, result.ordering->mode);
    TEST_ASSERT_EQUAL_INT(2, result.ordering->nplayers);
    TEST_ASSERT_TRUE(result.ordering->nentries > 0);
    enumResultFree(&result);
    TEST_ASSERT_NULL(result.ordering);
}

void test_enumResultAlloc_lo(void) {
    enum_result_t result;
    enumResultClear(&result);
    int err = enumResultAlloc(&result, 2, enum_ordering_mode_lo);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_lo, result.ordering->mode);
    enumResultFree(&result);
}

void test_enumResultAlloc_hilo(void) {
    enum_result_t result;
    enumResultClear(&result);
    int err = enumResultAlloc(&result, 2, enum_ordering_mode_hilo);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hilo, result.ordering->mode);
    enumResultFree(&result);
}

void test_enumResultAlloc_hihi(void) {
    enum_result_t result;
    enumResultClear(&result);
    int err = enumResultAlloc(&result, 2, enum_ordering_mode_hihi);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hihi, result.ordering->mode);
    enumResultFree(&result);
}

void test_enumResultAlloc_none(void) {
    enum_result_t result;
    enumResultClear(&result);
    int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NULL(result.ordering);
}

void test_enumResultFree_null_ordering(void) {
    enum_result_t result;
    enumResultClear(&result);
    /* Should not crash when ordering is NULL */
    enumResultFree(&result);
    TEST_ASSERT_NULL(result.ordering);
}

/* ===== enumExhaustive with orderflag tests ===== */

void test_enumerate_holdem_with_orderflag(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    /* Full board */
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* orderflag=1 triggers ordering allocation */
    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hi, result.ordering->mode);
    enumResultFree(&result);
}

void test_enumerate_omaha8_with_orderflag(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: Ac2d3h4s */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    /* Player 2: KsKhQsQh */
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    /* Full board */
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* orderflag=1 with hi/lo game -> hilo ordering */
    int err = enumExhaustive(game_omaha8, pockets, board, dead, 2, 5, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hilo, result.ordering->mode);
    enumResultFree(&result);
}

void test_enumerate_razz_with_orderflag(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: A-2-3-4-5-6-7 (full 7-card hand) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    /* Player 2: 2-3-4-5-6-7-8 (full 7-card hand) */
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Use MC with orderflag=1 for razz -> lo ordering (exhaustive too slow) */
    int err = enumSample(game_razz, pockets, board, dead, 2, 0, 100, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_lo, result.ordering->mode);
    enumResultFree(&result);
}

/* ===== Rare game types ===== */

/* badacey, badeucy, badugi and irish are defined in enum_gameparams
   (so enumGameParams() works), but their enumeration logic is NOT
   implemented in enumExhaustive/enumSample. Both functions fall through
   to the final else clause and return 1. We still call them to exercise
   the validation/dispatch code paths and verify they return 1. */

void test_enumerate_badacey(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Enumeration not implemented for badacey - expect error 1 */
    int err = enumSample(game_badacey, pockets, board, dead, 2, 0, 200, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
    enumResultFree(&result);
}

void test_enumerate_badeucy(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Enumeration not implemented for badeucy - expect error 1 */
    int err = enumSample(game_badeucy, pockets, board, dead, 2, 0, 200, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
    enumResultFree(&result);
}

void test_enumerate_badugi(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Badugi sampling is implemented (enumSampleBatched -> MC) */
    int err = enumSample(game_badugi, pockets, board, dead, 2, 0, 200, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    enumResultFree(&result);
}

void test_enumerate_courchevel(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: AcAdKcKd5h (5 hole cards) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    /* Player 2: QsQhJsJh4c */
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);

    /* Full board */
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_courchevel, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    enumResultFree(&result);
}

void test_enumerate_courchevel8(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: Ac2d3h4s5c (5 hole cards, good for low) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

    /* Player 2: KsKhQsQh6d */
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);

    /* Full board with low possible */
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_courchevel8, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    enumResultFree(&result);
}

void test_enumerate_irish(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: AcKcQcJc (4 hole cards) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    /* Player 2: 9h8h7h6h */
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);

    /* Full board */
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Irish enumeration selects the best two-card discard combination. */
    int err = enumExhaustive(game_irish, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    enumResultFree(&result);
}

/* ===== Monte Carlo for rare games ===== */

void test_enumerate_mc_badacey(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Enumeration not implemented for badacey - expect error 1 */
    int err = enumSample(game_badacey, pockets, board, dead, 2, 0, 100, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
    enumResultFree(&result);
}

void test_enumerate_mc_badeucy(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Enumeration not implemented for badeucy - expect error 1 */
    int err = enumSample(game_badeucy, pockets, board, dead, 2, 0, 100, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
    enumResultFree(&result);
}

void test_enumerate_mc_badugi(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Badugi sampling is implemented (enumSampleBatched -> MC) */
    int err = enumSample(game_badugi, pockets, board, dead, 2, 0, 100, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    enumResultFree(&result);
}

void test_enumerate_mc_courchevel(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);

    /* Flop */
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumSample(game_courchevel, pockets, board, dead, 2, 3, 500, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    enumResultFree(&result);
}

void test_enumerate_mc_irish(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Enumeration now implemented for irish - expect success 0 */
    int err = enumSample(game_irish, pockets, board, dead, 2, 3, 500, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    enumResultFree(&result);
}

/* ===== enumResultPrint and enumResultPrintTerse tests ===== */

void test_enumResultPrint_holdem(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* These just need to not crash - they print to stdout */
    enumResultPrint(&result, pockets, board);
    enumResultPrintTerse(&result, pockets, board);
    enumResultFree(&result);
}

void test_enumResultPrint_hilo(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha8, pockets, board, dead, 2, 5, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* Print hilo result - covers hilo branch of enumResultPrint */
    enumResultPrint(&result, pockets, board);
    enumResultPrintTerse(&result, pockets, board);
    enumResultFree(&result);
}

void test_enumResultPrint_lowonly(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_razz, pockets, board, dead, 2, 0, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);

    /* Print low-only result - covers lo branch */
    enumResultPrint(&result, pockets, board);
    enumResultPrintTerse(&result, pockets, board);
    enumResultFree(&result);
}

/* ===== Error/boundary tests ===== */

void test_enumerate_too_many_players(void) {
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS + 1], board, dead;
    enum_result_t result;

    for (int i = 0; i <= ENUM_MAXPLAYERS; i++)
        StdDeck_CardMask_RESET(pockets[i]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem, pockets, board, dead,
                             ENUM_MAXPLAYERS + 1, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
}

void test_enumerate_mc_too_many_players(void) {
    StdDeck_CardMask pockets[ENUM_MAXPLAYERS + 1], board, dead;
    enum_result_t result;

    for (int i = 0; i <= ENUM_MAXPLAYERS; i++)
        StdDeck_CardMask_RESET(pockets[i]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    enumResultClear(&result);

    int err = enumSample(game_holdem, pockets, board, dead,
                          ENUM_MAXPLAYERS + 1, 0, 100, 0, &result);
    TEST_ASSERT_EQUAL_INT(1, err);
}

void test_enumerate_invalid_game(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    enumResultClear(&result);

    int err = enumExhaustive(game_NUMGAMES, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_NOT_EQUAL(0, err);
}

void test_enumerate_invalid_nboard(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* Invalid nboard value (2 is invalid for holdem) */
    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 2, 0, &result);
    TEST_ASSERT_NOT_EQUAL(0, err);
}

void test_enumerate_mc_with_orderflag(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* MC with orderflag=1 */
    int err = enumSample(game_holdem, pockets, board, dead, 2, 0, 500, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);
    TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hi, result.ordering->mode);
    enumResultFree(&result);
}

/* ===== Omaha5/6 exhaustive tests ===== */

void test_enumerate_omaha5_full_board(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: AcAdKcKd5h */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    /* Player 2: QsQhJsJh4c */
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);

    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha5, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(1, result.nsamples);
    enumResultFree(&result);
}

void test_enumerate_omaha6_full_board(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Player 1: AcAdKcKd5h3s (6 hole cards) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_SPADES);

    /* Player 2: QsQhJsJh4c2d */
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha6, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(1, result.nsamples);
    enumResultFree(&result);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("=== test_enumerate_edge_cases starting ===\n");
    UNITY_BEGIN();

    /* enumGameParams tests */
    RUN_TEST(test_enumGameParams_holdem);
    RUN_TEST(test_enumGameParams_omaha8);
    RUN_TEST(test_enumGameParams_razz);
    RUN_TEST(test_enumGameParams_invalid);
    RUN_TEST(test_enumGameParams_all_games);

    /* enumResultAlloc/Free/Clear tests */
    RUN_TEST(test_enumResultAlloc_hi);
    RUN_TEST(test_enumResultAlloc_lo);
    RUN_TEST(test_enumResultAlloc_hilo);
    RUN_TEST(test_enumResultAlloc_hihi);
    RUN_TEST(test_enumResultAlloc_none);
    RUN_TEST(test_enumResultFree_null_ordering);

    /* Orderflag tests */
    RUN_TEST(test_enumerate_holdem_with_orderflag);
    RUN_TEST(test_enumerate_omaha8_with_orderflag);
    RUN_TEST(test_enumerate_razz_with_orderflag);

    /* Rare game types - exhaustive */
    RUN_TEST(test_enumerate_badacey);
    RUN_TEST(test_enumerate_badeucy);
    RUN_TEST(test_enumerate_badugi);
    RUN_TEST(test_enumerate_courchevel);
    RUN_TEST(test_enumerate_courchevel8);
    RUN_TEST(test_enumerate_irish);

    /* Rare game types - Monte Carlo */
    RUN_TEST(test_enumerate_mc_badacey);
    RUN_TEST(test_enumerate_mc_badeucy);
    RUN_TEST(test_enumerate_mc_badugi);
    RUN_TEST(test_enumerate_mc_courchevel);
    RUN_TEST(test_enumerate_mc_irish);

    /* Print function tests */
    RUN_TEST(test_enumResultPrint_holdem);
    RUN_TEST(test_enumResultPrint_hilo);
    RUN_TEST(test_enumResultPrint_lowonly);

    /* Error/boundary tests */
    RUN_TEST(test_enumerate_too_many_players);
    RUN_TEST(test_enumerate_mc_too_many_players);
    RUN_TEST(test_enumerate_invalid_game);
    RUN_TEST(test_enumerate_invalid_nboard);
    RUN_TEST(test_enumerate_mc_with_orderflag);

    /* Omaha5/6 tests */
    RUN_TEST(test_enumerate_omaha5_full_board);
    RUN_TEST(test_enumerate_omaha6_full_board);

    return UNITY_END();
}
