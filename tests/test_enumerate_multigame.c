/*
 * test_enumerate_multigame.c - Multi-game enumeration coverage tests
 *
 * Tests enumExhaustive() and enumSample() across all supported game types
 * to maximize code coverage in enumerate.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Helper: build dead mask from all pockets */
static void build_dead(StdDeck_CardMask *dead, StdDeck_CardMask pockets[], int npockets,
                       StdDeck_CardMask board) {
    StdDeck_CardMask_RESET(*dead);
    for (int i = 0; i < npockets; i++)
        StdDeck_CardMask_OR(*dead, *dead, pockets[i]);
    StdDeck_CardMask_OR(*dead, *dead, board);
}

/* ========== Hold'em ========== */

static void test_enumerate_holdem(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: As Kh, P2: 7c 2d */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

    /* Flop: 3c 5d 9h */
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 3, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_TRUE(result.nsamples > 0);
    TEST_ASSERT_TRUE(result.ev[0] > 0.0);
    TEST_ASSERT_TRUE(result.ev[1] > 0.0);
    TEST_ASSERT_TRUE(result.ev[0] + result.ev[1] > 0.99);
}

static void test_enumerate_holdem_full_board(void) {
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
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Hold'em Hi/Lo ========== */

static void test_enumerate_holdem8(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    /* Board with low cards to enable low hand */
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem8, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 has A2 with 3-4-5-7 on board: should have a low */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0 || result.ntielo[0] > 0);
}

/* ========== Omaha ========== */

static void test_enumerate_omaha(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: As Kh Qd Jc */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    /* P2: 9s 8h 7d 6c */
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Omaha 5-card ========== */

static void test_enumerate_omaha5(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha5, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Omaha 6-card ========== */

static void test_enumerate_omaha6(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);

    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha6, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Omaha Hi/Lo ========== */

static void test_enumerate_omaha8(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: Ah 2c 3d 4s - strong low */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
    /* P2: Ks Kc Qd Qh - strong high */
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    /* Board enabling low: 5h 6s 7c Jd 9h */
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_7, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_omaha8, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 should win the low */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0 || result.ntielo[0] > 0);
}

/* ========== 7-Card Stud ========== */

static void test_enumerate_7stud(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: 7 cards */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    /* P2: 7 cards */
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_7stud, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 has AA KK, P2 has QQ JJ -> P1 wins */
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);
}

/* ========== 7-Card Stud Hi/Lo ========== */

static void test_enumerate_7stud8(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: A 2 3 4 5 K Q - has A-5 low */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    /* P2: K K Q Q J 10 9 - no low */
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_7stud8, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 should win the low */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0);
}

/* ========== Razz ========== */

static void test_enumerate_razz(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: A 2 3 4 5 6 7 - wheel + 6,7 */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_6, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

    /* P2: 2 3 4 5 6 8 9 */
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_razz, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 has A-5 (best razz hand), should win */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0);
}

/* ========== 5-Card Draw ========== */

static void test_enumerate_5draw(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_5draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 full house AAA KK beats P2 two pair QQ JJ */
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
}

/* ========== Lowball A-5 ========== */

static void test_enumerate_lowball(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: A 2 3 4 5 (wheel) */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);

    /* P2: 2 3 4 5 7 */
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_lowball, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 wheel beats P2 */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0);
}

/* ========== Lowball 2-7 ========== */

static void test_enumerate_lowball27(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* P1: 2 3 4 5 7 (best 2-7 hand, different suits) */
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_SPADES);

    /* P2: 2 3 4 5 8 */
    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_lowball27, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* P1 7-low beats P2 8-low */
    TEST_ASSERT_TRUE(result.nwinlo[0] > 0);
}

/* ========== Short Deck Hold'em ========== */

static void test_enumerate_shortdeck(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Use high cards only (8+) for short deck */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_8, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_sdholdem, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Pineapple ========== */

static void test_enumerate_pineapple(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* 3 hole cards each */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_pineapple, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Drawmaha ========== */

static void test_enumerate_drawmaha(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* 5 hole cards each */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_drawmaha, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== 2-7 Triple Draw ========== */

static void test_enumerate_27_triple_draw(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_27_triple_draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== A-5 Triple Draw ========== */

static void test_enumerate_a5_triple_draw(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_a5_triple_draw, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Courchevel ========== */

static void test_enumerate_courchevel(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* 5 hole cards each */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&pockets[0], StdDeck_Rank_TEN, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_courchevel, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Fusion ========== */

static void test_enumerate_fusion(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_fusion, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Irish ========== */

static void test_enumerate_irish(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* 4 hole cards each */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_irish, pockets, board, dead, 2, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
}

/* ========== Monte Carlo ========== */

static void test_sample_holdem(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumSample(game_holdem, pockets, board, dead, 2, 0, 5000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(5000, result.nsamples);
    /* AA vs KK: AA should win ~82% (ev is raw win+½split points; divide by nsamples) */
    TEST_ASSERT_TRUE((result.ev[0] / result.nsamples) > 0.75);
    TEST_ASSERT_TRUE((result.ev[0] / result.nsamples) < 0.90);
}

static void test_sample_omaha(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumSample(game_omaha, pockets, board, dead, 2, 0, 5000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(5000, result.nsamples);
    TEST_ASSERT_TRUE(result.ev[0] > 0.0);
    TEST_ASSERT_TRUE(result.ev[1] > 0.0);
}

static void test_sample_7stud(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);

    /* Give 3 cards each, let MC deal remaining */
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);

    add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
    add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);
    add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    int err = enumSample(game_7stud, pockets, board, dead, 2, 0, 5000, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(5000, result.nsamples);
    /* AA K should dominate QJT */
    TEST_ASSERT_TRUE(result.ev[0] > 0.5);
}

/* ========== Utility Functions ========== */

static void test_enumGameParams_all_games(void) {
    enum_game_t games[] = {
        game_holdem, game_holdem8, game_omaha, game_omaha5, game_omaha6,
        game_omaha8, game_omaha85, game_omaha86, game_7stud, game_7stud8,
        game_7studnsq, game_razz, game_5draw, game_5draw8, game_5drawnsq,
        game_lowball, game_lowball27, game_sdholdem, game_doubleflop_holdem,
        game_drawmaha, game_pineapple, game_27_triple_draw, game_a5_triple_draw,
        game_badacey, game_badeucy, game_badugi, game_fusion, game_courchevel,
        game_courchevel8, game_irish, game_ofc, game_manila
    };
    int ngames = sizeof(games) / sizeof(games[0]);

    for (int i = 0; i < ngames; i++) {
        enum_gameparams_t *params = enumGameParams(games[i]);
        TEST_ASSERT_NOT_NULL(params);
        TEST_ASSERT_TRUE(params->maxpocket > 0);
        TEST_ASSERT_NOT_NULL(params->name);
    }
}

static void test_enumResultAlloc_free(void) {
    enum_result_t result;
    enumResultClear(&result);

    int err = enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);

    enumResultFree(&result);
    TEST_ASSERT_NULL(result.ordering);
}

static void test_enumResultClear(void) {
    enum_result_t result;
    memset(&result, 0xFF, sizeof(result));

    enumResultClear(&result);
    TEST_ASSERT_EQUAL_UINT(0, result.nsamples);
    TEST_ASSERT_EQUAL_INT(0, result.nwinhi[0]);
    TEST_ASSERT_EQUAL_INT(0, result.nwinlo[0]);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, result.ev[0]);
}

static void test_enumResultPrint(void) {
    /* Just verify it doesn't crash */
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
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);
    /* Should not crash */
    enumResultPrint(&result, pockets, board);
    enumResultPrintTerse(&result, pockets, board);
    TEST_PASS();
}

/* ========== Edge Cases ========== */

static void test_enumerate_3player(void) {
    StdDeck_CardMask pockets[3], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(pockets[2]);
    StdDeck_CardMask_RESET(board);

    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
    add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
    add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

    add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
    add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
    add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

    build_dead(&dead, pockets, 3, board);
    enumResultClear(&result);

    int err = enumExhaustive(game_holdem, pockets, board, dead, 3, 5, 0, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_UINT(1, result.nsamples);
    /* AA should win */
    TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
}

static void test_enumerate_with_ordering(void) {
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
    add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
    add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);

    build_dead(&dead, pockets, 2, board);
    enumResultClear(&result);

    /* orderflag=1 triggers enumResultAlloc for ordering histogram */
    int err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 1, &result);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(result.ordering);

    enumResultFree(&result);
}

static void test_enumerate_invalid_game(void) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    enumResultClear(&result);
    int err = enumExhaustive(game_NUMGAMES, pockets, board, dead, 2, 0, 0, &result);
    TEST_ASSERT_TRUE(err != 0);
}

/* ========== Main ========== */

int main(void) {
    UNITY_BEGIN();

    /* Hold'em family */
    RUN_TEST(test_enumerate_holdem);
    RUN_TEST(test_enumerate_holdem_full_board);
    RUN_TEST(test_enumerate_holdem8);
    RUN_TEST(test_enumerate_fusion);
    RUN_TEST(test_enumerate_irish);

    /* Omaha family */
    RUN_TEST(test_enumerate_omaha);
    RUN_TEST(test_enumerate_omaha5);
    RUN_TEST(test_enumerate_omaha6);
    RUN_TEST(test_enumerate_omaha8);
    RUN_TEST(test_enumerate_courchevel);

    /* Stud family */
    RUN_TEST(test_enumerate_7stud);
    RUN_TEST(test_enumerate_7stud8);
    RUN_TEST(test_enumerate_razz);

    /* Draw family */
    RUN_TEST(test_enumerate_5draw);
    RUN_TEST(test_enumerate_lowball);
    RUN_TEST(test_enumerate_lowball27);
    RUN_TEST(test_enumerate_27_triple_draw);
    RUN_TEST(test_enumerate_a5_triple_draw);

    /* Other */
    RUN_TEST(test_enumerate_shortdeck);
    RUN_TEST(test_enumerate_pineapple);
    RUN_TEST(test_enumerate_drawmaha);

    /* Monte Carlo */
    RUN_TEST(test_sample_holdem);
    RUN_TEST(test_sample_omaha);
    RUN_TEST(test_sample_7stud);

    /* Utility functions */
    RUN_TEST(test_enumGameParams_all_games);
    RUN_TEST(test_enumResultAlloc_free);
    RUN_TEST(test_enumResultClear);
    RUN_TEST(test_enumResultPrint);

    /* Edge cases */
    RUN_TEST(test_enumerate_3player);
    RUN_TEST(test_enumerate_with_ordering);
    RUN_TEST(test_enumerate_invalid_game);

    return UNITY_END();
}
