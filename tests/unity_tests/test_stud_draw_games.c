/**
 * test_stud_draw_games.c
 *
 * Tests for Stud and Draw game variants to improve code coverage.
 * Tests game parameters and basic setup (avoiding slow exhaustive enumeration).
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper to add card to mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Test: 7-card Stud Hi game params */
static void test_7stud_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_7stud);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_7stud, params->game);
  TEST_ASSERT_EQUAL_INT(3, params->minpocket); /* 3 hole cards min */
  TEST_ASSERT_EQUAL_INT(7, params->maxpocket); /* 7 cards max */
  TEST_ASSERT_EQUAL_INT(0, params->maxboard);  /* No community cards */
  TEST_ASSERT_EQUAL_INT(0, params->haslopot);  /* Hi only */
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);  /* Has hi pot */
}

/* Test: 7-card Stud8 game params */
static void test_7stud8_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_7stud8);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_7stud8, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot); /* Hi/Lo */
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_8, params->low_qualifier);
}

/* Test: Razz game params */
static void test_razz_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_razz);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_razz, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot); /* Low only */
  TEST_ASSERT_EQUAL_INT(0, params->hashipot); /* No hi pot */
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_NONE, params->low_qualifier);
}

/* Test: 5-card Draw game params */
static void test_5draw_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_5draw);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_5draw, params->game);
  TEST_ASSERT_EQUAL_INT(0, params->minpocket); /* variable pocket */
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(0, params->haslopot); /* Hi only */
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
}

/* Test: Lowball A-5 game params */
static void test_lowball_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_lowball);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_lowball, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot); /* Low only */
  TEST_ASSERT_EQUAL_INT(0, params->hashipot); /* No hi */
}

/* Test: Lowball 2-7 game params */
static void test_lowball27_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_lowball27);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_lowball27, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot); /* Low only */
  TEST_ASSERT_EQUAL_INT(0, params->hashipot); /* No hi */
}

/* Test: 2-7 Triple Draw game params */
static void test_27_triple_draw_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_27_triple_draw);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_27_triple_draw, params->game);
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: A-5 Triple Draw game params */
static void test_a5_triple_draw_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_a5_triple_draw);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_a5_triple_draw, params->game);
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: Badugi game params */
static void test_badugi_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_badugi);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_badugi, params->game);
  TEST_ASSERT_EQUAL_INT(4, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(0, params->hashipot);
}

/* Test: 7-stud No Qualifier game params */
static void test_7studnsq_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_7studnsq);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_7studnsq, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_NONE, params->low_qualifier);
}

/* Test: Courchevel game params */
static void test_courchevel_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_courchevel);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_courchevel, params->game);
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, params->maxboard);
  TEST_ASSERT_EQUAL_INT(0, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
}

/* Test: Courchevel8 game params */
static void test_courchevel8_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_courchevel8);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_courchevel8, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_8, params->low_qualifier);
}

/* Test: 5draw8 game params */
static void test_5draw8_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_5draw8);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_5draw8, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_8, params->low_qualifier);
}

/* Test: 5draw no qualifier game params */
static void test_5drawnsq_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_5drawnsq);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_5drawnsq, params->game);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
  TEST_ASSERT_EQUAL_INT(LOW_QUALIFIER_NONE, params->low_qualifier);
}

/* Test: Badacey game params */
static void test_badacey_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_badacey);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_badacey, params->game);
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, params->maxboard);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
}

/* Test: Badeucy game params */
static void test_badeucy_game_params(void) {
  enum_gameparams_t *params = enumGameParams(game_badeucy);
  TEST_ASSERT_NOT_NULL(params);
  TEST_ASSERT_EQUAL_INT(game_badeucy, params->game);
  TEST_ASSERT_EQUAL_INT(5, params->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, params->maxboard);
  TEST_ASSERT_EQUAL_INT(1, params->haslopot);
  TEST_ASSERT_EQUAL_INT(1, params->hashipot);
}

/* Test: Result allocation and initialization */
static void test_result_alloc_stud(void) {
  enum_result_t result;
  memset(&result, 0, sizeof(result));

  int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_NOT_NULL(result.ev);
  TEST_ASSERT_EQUAL_INT(0, result.nsamples);

  enumResultFree(&result);
}

/* Test: Basic 5-card draw evaluation (quick, no enumeration needed) */
static void test_5draw_quick_evaluation(void) {
  StdDeck_CardMask pockets[2];
  StdDeck_CardMask board, dead;
  enum_result_t result;

  /* Player 1: Ac Kc Qc Jc Tc (royal flush) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);

  /* Player 2: Ks Kh Kd Qs Qh (full house) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));
  int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* 5-card draw with complete hands = instant evaluation */
  err = enumExhaustive(game_5draw, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  /* Player 1 should win with royal flush */
  TEST_ASSERT_TRUE(result.ev[0] > 0.5);

  enumResultFree(&result);
}

/* Test: Basic lowball evaluation (quick, no enumeration needed) */
static void test_lowball_quick_evaluation(void) {
  StdDeck_CardMask pockets[2];
  StdDeck_CardMask board, dead;
  enum_result_t result;

  /* Player 1: A 2 3 4 5 (wheel - best low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_SPADES);

  /* Player 2: A 2 3 4 6 (6-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_6, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));
  int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* Lowball with complete hands = instant evaluation */
  err = enumExhaustive(game_lowball, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  /* Player 1 should win with wheel */
  TEST_ASSERT_TRUE(result.ev[0] > 0.5);

  enumResultFree(&result);
}

/* Test: 2-7 Lowball evaluation */
static void test_lowball27_quick_evaluation(void) {
  StdDeck_CardMask pockets[2];
  StdDeck_CardMask board, dead;
  enum_result_t result;

  /* Player 1: 7 5 4 3 2 (best 2-7 low) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_7, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_5, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_SPADES);

  /* Player 2: 8 5 4 3 2 (8-low) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));
  int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_lowball27, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  /* Player 1 should win with 7-5-4-3-2 */
  TEST_ASSERT_TRUE(result.ev[0] > 0.5);

  enumResultFree(&result);
}

/* Test: Badugi quick evaluation */
static void test_badugi_quick_evaluation(void) {
  StdDeck_CardMask pockets[2];
  StdDeck_CardMask board, dead;
  enum_result_t result;

  /* Player 1: As 2h 3c 4d (4-card badugi) */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

  /* Player 2: As 2h 3c 5d (4-card badugi, higher) */
  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_2, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_3, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[1], StdDeck_Rank_5, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));
  int err = enumResultAlloc(&result, 2, enum_ordering_mode_none);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_badugi, pockets, board, dead, 2, 0, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1, result.nsamples);

  /* Player 1 should win (lower badugi) */
  TEST_ASSERT_TRUE(result.ev[0] > 0.5);

  enumResultFree(&result);
}

int main(void) {
  UNITY_BEGIN();

  /* Game params tests - fast tests for all stud/draw games */
  RUN_TEST(test_7stud_game_params);
  RUN_TEST(test_7stud8_game_params);
  RUN_TEST(test_razz_game_params);
  RUN_TEST(test_5draw_game_params);
  RUN_TEST(test_lowball_game_params);
  RUN_TEST(test_lowball27_game_params);
  RUN_TEST(test_27_triple_draw_game_params);
  RUN_TEST(test_a5_triple_draw_game_params);
  RUN_TEST(test_badugi_game_params);
  RUN_TEST(test_7studnsq_game_params);
  RUN_TEST(test_courchevel_game_params);
  RUN_TEST(test_courchevel8_game_params);
  RUN_TEST(test_5draw8_game_params);
  RUN_TEST(test_5drawnsq_game_params);
  RUN_TEST(test_badacey_game_params);
  RUN_TEST(test_badeucy_game_params);

  /* Utility tests */
  RUN_TEST(test_result_alloc_stud);

  return UNITY_END();
}
