/*
 * test_pokenum_integration.c -- integration tests exercising full evaluation
 * pipeline
 *
 * Tests the complete evaluation flow similar to pokenum:
 * - Parse cards
 * - Setup enumeration parameters
 * - Run enumeration (exhaustive or Monte Carlo)
 * - Verify results
 *
 * These tests validate end-to-end behavior across multiple game types.
 */

#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/enumord.h>

/* Tolerance for Monte Carlo results */
#define MC_TOLERANCE 0.05

/* Helper to add a card to a mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Build dead cards from pockets and board */
static void build_dead(StdDeck_CardMask *dead, StdDeck_CardMask pockets[],
                       int npockets, StdDeck_CardMask board) {
  StdDeck_CardMask_RESET(*dead);
  for (int i = 0; i < npockets; i++)
    StdDeck_CardMask_OR(*dead, *dead, pockets[i]);
  StdDeck_CardMask_OR(*dead, *dead, board);
}

/* Calculate equity from result */
static double get_equity(const enum_result_t *result, int player) {
  return result->ev[player];
}

void setUp(void) {}
void tearDown(void) {}

/* ===== HOLDEM INTEGRATION TESTS ===== */

/*
 * Test: AcKc vs 7h2d (classic mismatch)
 * AK suited should dominate 72 offsuit
 */
void test_pokenum_holdem_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: 7h2d (the worst hand in poker) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

  /* Full board: 3s4h5c6d8s (straight possible for 72) */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples); /* Exact: 1 outcome */

  enumResultFree(&result);
}

/*
 * Test: 3-player holdem (AA vs KK vs QQ)
 */
void test_pokenum_holdem_3p(void) {
  StdDeck_CardMask pockets[3], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AsAh */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  /* Player 2: KsKh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

  /* Player 3: QsQh */
  StdDeck_CardMask_RESET(pockets[2]);
  add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 3, board);

  enumResultAlloc(&result, 3, enum_ordering_mode_hi);

  err = enumExhaustive(game_holdem, pockets, board, dead, 3, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* AA should win */
  TEST_ASSERT_GREATER_THAN_DOUBLE(result.ev[1], result.ev[0]);
  TEST_ASSERT_GREATER_THAN_DOUBLE(result.ev[2], result.ev[0]);

  enumResultFree(&result);
}

/* ===== OMAHA INTEGRATION TESTS ===== */

/*
 * Test: Omaha 2-player (AAKKds vs QQJJds)
 */
void test_pokenum_omaha_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcAdKcKd (double suited) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

  /* Player 2: QsQhJsJh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_omaha, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  enumResultFree(&result);
}

/*
 * Test: Omaha Hi/Lo with scoop potential
 */
void test_pokenum_omaha8_hilo(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2c3d4h (great for low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);

  /* Player 2: KsKhQsQh (no low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Board with low possible: 5c6d7h8sKc */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hilo);

  err = enumExhaustive(game_omaha8, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 should have low equity */
  TEST_ASSERT_GREATER_THAN(0, result.nwinlo[0] + result.ntielo[0]);

  enumResultFree(&result);
}

/* ===== 7-CARD STUD INTEGRATION TESTS ===== */

/*
 * Test: 7-stud 2-player
 */
void test_pokenum_7stud_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AsAhAdKcKd (full house aces full) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

  /* Player 2: QsQhQdJsJh (full house queens full) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);

  /* No board for stud */
  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_7stud, pockets, board, dead, 2, 0, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (aces full) beats player 2 (queens full) */
  TEST_ASSERT_EQUAL_INT(1, result.nwinhi[0]);
  TEST_ASSERT_EQUAL_INT(0, result.nwinhi[1]);

  enumResultFree(&result);
}

/* ===== RAZZ INTEGRATION TESTS ===== */

/*
 * Test: Razz 2-player (low hand wins)
 */
void test_pokenum_razz_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: A-2-3-4-5-6-7 (perfect wheel) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

  /* Player 2: 2-3-4-5-6-7-8 */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_lo);

  err = enumExhaustive(game_razz, pockets, board, dead, 2, 0, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (wheel) beats player 2 (8-low) */
  TEST_ASSERT_EQUAL_INT(1, result.nwinlo[0]);

  enumResultFree(&result);
}

/* ===== LOWBALL INTEGRATION TESTS ===== */

/*
 * Test: A-5 Lowball (wheel is best)
 */
void test_pokenum_lowball_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: A-2-3-4-5 (wheel) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

  /* Player 2: A-2-3-4-6 (6-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_lo);

  err = enumExhaustive(game_lowball, pockets, board, dead, 2, 0, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (wheel) beats player 2 (6-low) */
  TEST_ASSERT_EQUAL_INT(1, result.nwinlo[0]);

  enumResultFree(&result);
}

/*
 * Test: 2-7 Lowball (7-5 is best, straights/flushes count against)
 */
void test_pokenum_lowball27_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: 2-3-4-5-7 (best 2-7 low, no straight/flush) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

  /* Player 2: 2-3-4-5-8 (8-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_lo);

  err = enumExhaustive(game_lowball27, pockets, board, dead, 2, 0, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (7-5) beats player 2 (8-5) */
  TEST_ASSERT_EQUAL_INT(1, result.nwinlo[0]);

  enumResultFree(&result);
}

/* ===== SHORT DECK HOLDEM TESTS ===== */

/*
 * Test: Short deck holdem (36 cards, 6+ only)
 */
void test_pokenum_shortdeck_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: QsQh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Full board with short deck cards (6+) */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_sdholdem, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== BOARD VARIANTS TESTS ===== */

/*
 * Test: Holdem with partial board (turn card remaining)
 */
void test_pokenum_with_board(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: QsQh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Flop only (turn+river to come) */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_holdem, pockets, board, dead, 2, 3, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* Should enumerate C(45,2) = 990 runouts */
  TEST_ASSERT_EQUAL_INT(990, result.nsamples);

  enumResultFree(&result);
}

/*
 * Test: Holdem with dead cards specified
 */
void test_pokenum_with_dead_cards(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: QsQh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

  /* Additional dead cards: Ad, Ah (aces burned) */
  build_dead(&dead, pockets, 2, board);
  add_card(&dead, StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&dead, StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== MONTE CARLO MODE TESTS ===== */

/*
 * Test: Monte Carlo sampling produces reasonable results
 */
void test_pokenum_mc_mode(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: 7h2d */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

  /* No board (preflop) */
  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  /* Monte Carlo with 10000 samples */
  err = enumSample(game_holdem, pockets, board, dead, 2, 0, 10000, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(10000, result.nsamples);
  /* AKs should be ~65% vs 72o preflop */
  TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, 0.65, result.ev[0] / result.nsamples);

  enumResultFree(&result);
}

/* ===== KNOWN EQUITY VERIFICATION TESTS ===== */

/*
 * Test: AA vs KK preflop equity (well-known ~82% vs ~18%)
 */
void test_pokenum_known_equity(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AsAh */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  /* Player 2: KsKh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

  /* No board (preflop) */
  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  /* Monte Carlo with high sample count for accuracy */
  err = enumSample(game_holdem, pockets, board, dead, 2, 0, 50000, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);
  /* AA vs KK is approximately 82% vs 18% */
  TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, 0.82, result.ev[0] / result.nsamples);
  TEST_ASSERT_DOUBLE_WITHIN(MC_TOLERANCE, 0.18, result.ev[1] / result.nsamples);

  enumResultFree(&result);
}

/* ===== PINEAPPLE TESTS ===== */

/*
 * Test: Pineapple (3 hole cards)
 */
void test_pokenum_pineapple_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcKcQc (3 cards) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

  /* Player 2: 7h6h5h (3 cards) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_pineapple, pockets, board, dead, 2, 5, 0, &result);

  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== DISPATCH BACKEND TESTS ===== */

/*
 * Test: Dispatch routes correctly and produces valid results
 */
void test_pokenum_dispatch_integration(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t dispatch_result, classic_result;
  int dispatch_err, classic_err;

  /* Player 1: AcKc */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: QsQh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&dispatch_result, 2, enum_ordering_mode_hi);
  enumResultAlloc(&classic_result, 2, enum_ordering_mode_hi);

  /* Dispatch version */
  dispatch_err = enumExhaustive_dispatch(game_holdem, pockets, board, dead, 2,
                                         5, 0, &dispatch_result);

  /* Classic version */
  classic_err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 0,
                               &classic_result);

  TEST_ASSERT_EQUAL_INT(0, dispatch_err);
  TEST_ASSERT_EQUAL_INT(0, classic_err);

  /* Results should match */
  TEST_ASSERT_EQUAL_INT(classic_result.nsamples, dispatch_result.nsamples);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, classic_result.ev[0], dispatch_result.ev[0]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, classic_result.ev[1], dispatch_result.ev[1]);

  enumResultFree(&dispatch_result);
  enumResultFree(&classic_result);
}

/* ===== PLO5 TESTS ===== */

void test_pokenum_plo5_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcAdKcKd5h (5 hole cards) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

  /* Player 2: QsQhJsJh4c */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);
  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_omaha5, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  enumResultFree(&result);
}

/* ===== PLO6 TESTS ===== */

void test_pokenum_plo6_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcAdKcKd5h3s (6 hole cards) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_SPADES);

  /* Player 2: QsQhJsJh4c2d */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);
  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_omaha6, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  enumResultFree(&result);
}

/* ===== COURCHEVEL TESTS ===== */

void test_pokenum_courchevel_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: AcAdKcKd5h */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);

  /* Player 2: QsQhJsJh4c */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);

  /* Full board */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);
  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_courchevel, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== 7-STUD HI/LO TESTS ===== */

void test_pokenum_7stud8_hilo(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2d3h4s5c6d7h (7 cards, good for low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

  /* Player 2: KsKhKdQsQhQd8c (trips) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hilo);

  err = enumExhaustive(game_7stud8, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== 2-7 TRIPLE DRAW TESTS ===== */

void test_pokenum_27_triple_draw(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: 2c3d4h5s7c (best 2-7 low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

  /* Player 2: 2s3c4d6h8s (8-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_lo);

  err = enumExhaustive(game_27_triple_draw, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (7-5) should win */
  TEST_ASSERT_EQUAL_INT(1, result.nwinlo[0]);

  enumResultFree(&result);
}

/* ===== A-5 TRIPLE DRAW TESTS ===== */

void test_pokenum_a5_triple_draw(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2d3h4s5c (wheel) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_CLUBS);

  /* Player 2: As2c3d4h6s (6-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_lo);

  err = enumExhaustive(game_a5_triple_draw, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 (wheel) should win */
  TEST_ASSERT_EQUAL_INT(1, result.nwinlo[0]);

  enumResultFree(&result);
}

/* ===== BADUGI TESTS ===== */

void test_pokenum_badugi_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2d3h4s (perfect badugi) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);

  /* Player 2: 5c6d7h8s */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(board);
  build_dead(&dead, pockets, 2, board);

  enumResultAlloc(&result, 2, enum_ordering_mode_hi);

  err = enumExhaustive(game_badugi, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_TRUE(result.nsamples > 0);

  enumResultFree(&result);
}

/* ===== OMAHA 85 (5-card Omaha Hi/Lo) TESTS ===== */

void test_pokenum_omaha85_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2d3h4sKc (5 cards, good for low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  /* Player 2: KsKhQsQh5d */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);

  /* Board with low possible */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);
  enumResultAlloc(&result, 2, enum_ordering_mode_hilo);

  err = enumExhaustive(game_omaha85, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  enumResultFree(&result);
}

/* ===== HOLDEM 8 (Hi/Lo) TESTS ===== */

void test_pokenum_holdem8_2p(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Player 1: Ac2d */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

  /* Player 2: KsKh */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

  /* Board with low possible */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_CLUBS);

  build_dead(&dead, pockets, 2, board);
  enumResultAlloc(&result, 2, enum_ordering_mode_hilo);

  err = enumExhaustive(game_holdem8, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  /* Player 1 should win the low */
  TEST_ASSERT_TRUE(result.nwinlo[0] > 0 || result.ntielo[0] > 0);

  enumResultFree(&result);
}

int main(void) {
  UNITY_BEGIN();

  /* Holdem integration tests */
  RUN_TEST(test_pokenum_holdem_2p);
  RUN_TEST(test_pokenum_holdem_3p);

  /* Omaha integration tests */
  RUN_TEST(test_pokenum_omaha_2p);
  RUN_TEST(test_pokenum_omaha8_hilo);

  /* Stud integration tests */
  RUN_TEST(test_pokenum_7stud_2p);

  /* Low games integration tests */
  RUN_TEST(test_pokenum_razz_2p);
  RUN_TEST(test_pokenum_lowball_2p);
  RUN_TEST(test_pokenum_lowball27_2p);

  /* Other game variants */
  RUN_TEST(test_pokenum_shortdeck_2p);
  RUN_TEST(test_pokenum_pineapple_2p);

  /* Board and dead card tests */
  RUN_TEST(test_pokenum_with_board);
  RUN_TEST(test_pokenum_with_dead_cards);

  /* Monte Carlo tests */
  RUN_TEST(test_pokenum_mc_mode);
  RUN_TEST(test_pokenum_known_equity);

  /* Dispatch integration test */
  RUN_TEST(test_pokenum_dispatch_integration);

  /* Phase 4: New game variant tests */
  RUN_TEST(test_pokenum_plo5_2p);
  RUN_TEST(test_pokenum_plo6_2p);
  RUN_TEST(test_pokenum_courchevel_2p);
  RUN_TEST(test_pokenum_7stud8_hilo);
  RUN_TEST(test_pokenum_27_triple_draw);
  RUN_TEST(test_pokenum_a5_triple_draw);
  RUN_TEST(test_pokenum_badugi_2p);
  RUN_TEST(test_pokenum_omaha85_2p);
  RUN_TEST(test_pokenum_holdem8_2p);

  return UNITY_END();
}
