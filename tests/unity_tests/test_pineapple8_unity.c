/**
 * test_pineapple8_unity.c
 *
 * Unity tests for Pineapple Hi/Lo (game_pineapple8): the player keeps three
 * hole cards and chooses the best two independently for the high and low
 * hand against the five-card board (8-or-better low qualifier).
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/enumord.h>
#include <poker_eval/core/low_qualifier.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

static void test_pineapple8_registered(void) {
  enum_gameparams_t *gp = enumGameParams(game_pineapple8);
  TEST_ASSERT_NOT_NULL(gp);
  TEST_ASSERT_EQUAL_INT(3, gp->minpocket);
  TEST_ASSERT_EQUAL_INT(3, gp->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, gp->maxboard);
  TEST_ASSERT_EQUAL_INT(1, gp->haslopot);
  TEST_ASSERT_EQUAL_INT(1, gp->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_8, gp->low_qualifier);
}

static void test_pineapple8_wheel_low(void) {
  /* Player 0 holds 2c Ad Kc, Player 1 holds 7h Qh 9h. Board 3s 4d 5c 6h 2d.
     Player 0 can make the A2345 wheel for low with 2c + Ad, and wins it. */
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  enumResultClear(&result);
  err = enumExhaustive(game_pineapple8, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, (int)result.nsamples);

  /* Player 0 wins the low outright; player 1 wins the high. */
  TEST_ASSERT_EQUAL_INT(1, (int)result.nwinlo[0]);
  TEST_ASSERT_EQUAL_INT(1, (int)result.nloselo[1]);
  TEST_ASSERT_EQUAL_INT(1, (int)result.nwinhi[1]);
  TEST_ASSERT_EQUAL_INT(0, (int)result.nloselo[0]);
  TEST_ASSERT_EQUAL_INT(0, (int)result.nscoop[0]);

  enumResultFree(&result);
}

static void test_pineapple8_no_qualifying_high_no_low(void) {
  /* Player 0 holds 2c Ad Kc, Player 1 holds 7h Qh 9h. Board 3s 4d 5c Kh Qs.
     Player 0's wheel low qualifies and wins the low half. */
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_3, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  enumResultClear(&result);
  err = enumExhaustive(game_pineapple8, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, (int)result.nsamples);

  TEST_ASSERT_EQUAL_INT(1, (int)result.nwinlo[0]);
  enumResultFree(&result);
}

static void test_pineapple8_mc_runs(void) {
  /* Monte Carlo path must run without error and produce sane totals. */
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);

  enumResultClear(&result);
  err = enumSample(game_pineapple8, pockets, board, dead, 2, 0, 2000, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(2000, (int)result.nsamples);

  /* EV must be in [0,1] for both players across the whole pot. */
  double total_ev = 0.0;
  for (int i = 0; i < 2; i++) {
    double ev = (double)result.ev[i] / (double)result.nsamples;
    TEST_ASSERT_TRUE(ev >= 0.0 && ev <= 1.0);
    total_ev += ev;
  }
  /* Both players split high+low portions; total EV should be near 1.0. */
  TEST_ASSERT_TRUE(total_ev > 0.9 && total_ev < 1.1);

  enumResultFree(&result);
}

static void test_pineapple_mc_still_runs(void) {
  /* Standard pineapple MC path (previously missing) must also run. */
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);

  enumResultClear(&result);
  err = enumSample(game_pineapple, pockets, board, dead, 2, 0, 2000, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(2000, (int)result.nsamples);
  enumResultFree(&result);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pineapple8_registered);
  RUN_TEST(test_pineapple8_wheel_low);
  RUN_TEST(test_pineapple8_no_qualifying_high_no_low);
  RUN_TEST(test_pineapple8_mc_runs);
  RUN_TEST(test_pineapple_mc_still_runs);
  return UNITY_END();
}