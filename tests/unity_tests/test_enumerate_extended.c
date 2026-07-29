/**
 * test_enumerate_extended.c
 *
 * Extended tests for enumerate.c to improve code coverage.
 * Tests all game types, print functions, error paths, and edge cases.
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/enumord.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper to add card to mask */
static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* Test enumGameParams for ALL game types */
static void test_all_game_params(void) {
  /* Test all games have valid parameters */
  struct {
    enum_game_t game;
    const char *name;
    int minpocket;
    int maxpocket;
  } games[] = {
      {game_holdem, "Holdem", 2, 2},
      {game_holdem8, "Holdem8", 2, 2},
      {game_omaha, "Omaha", 4, 4},
      {game_omaha5, "Omaha5", 5, 5},
      {game_omaha6, "Omaha6", 6, 6},
      {game_omaha8, "Omaha8", 4, 4},
      {game_omaha85, "Omaha85", 5, 5},
      {game_omaha86, "Omaha86", 6, 6},
      {game_7stud, "7Stud", 3, 7},
      {game_7stud8, "7Stud8", 3, 7},
      {game_7studnsq, "7StudNSQ", 3, 7},
      {game_razz, "Razz", 3, 7},
      {game_5draw, "5Draw", 0, 5},
      {game_5draw8, "5Draw8", 0, 5},
      {game_5drawnsq, "5DrawNSQ", 0, 5},
      {game_lowball, "Lowball", 0, 5},
      {game_lowball27, "Lowball27", 0, 5},
      {game_sdholdem, "ShortDeck", 2, 2},
      {game_pineapple, "Pineapple", 3, 3},
      {game_drawmaha, "Drawmaha", 5, 5},
      {game_courchevel, "Courchevel", 5, 5},
      {game_courchevel8, "Courchevel8", 5, 5},
      {game_27_triple_draw, "27TD", 5, 5},
      {game_a5_triple_draw, "A5TD", 5, 5},
      {game_badugi, "Badugi", 4, 4},
      {game_badacey, "Badacey", 5, 5},
      {game_badeucy, "Badeucy", 5, 5},
      {game_fusion, "Fusion", 2, 2},
      {game_irish, "Irish", 4, 4},
      {game_doubleflop_holdem, "DoubleFlop", 2, 2},
      {game_ofc, "OFC", 13, 13},
      {game_manila, "Manila", 2, 2},
  };

  size_t num_games = sizeof(games) / sizeof(games[0]);

  for (size_t i = 0; i < num_games; i++) {
    enum_gameparams_t *params = enumGameParams(games[i].game);
    TEST_ASSERT_NOT_NULL_MESSAGE(params, games[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(games[i].minpocket, params->minpocket,
                                  games[i].name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(games[i].maxpocket, params->maxpocket,
                                  games[i].name);
  }
}

/* Test invalid game type returns NULL */
static void test_invalid_game_params(void) {
  enum_gameparams_t *params;

  /* Test invalid game number returns NULL */
  params = enumGameParams((enum_game_t)-1);
  TEST_ASSERT_NULL(params);

  params = enumGameParams((enum_game_t)100);
  TEST_ASSERT_NULL(params);

  params = enumGameParams(game_NUMGAMES);
  TEST_ASSERT_NULL(params);
}

/* Test enumResultAlloc with max players for ordering histograms.
 * Note: ENUM_ORDERING_MAXPLAYERS (7) != ENUM_MAXPLAYERS (12).
 * Ordering histograms can only be allocated for up to 7 players. */
static void test_result_alloc_max_players(void) {
  enum_result_t result;
  int err;

  memset(&result, 0, sizeof(result));
  /* Use ENUM_ORDERING_MAXPLAYERS (7), not ENUM_MAXPLAYERS (12) */
  err =
      enumResultAlloc(&result, ENUM_ORDERING_MAXPLAYERS, enum_ordering_mode_hi);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_NOT_NULL(result.ev);
  TEST_ASSERT_NOT_NULL(result.ordering);
  enumResultFree(&result);
}

/* Test Hi/Lo ordering mode */
static void test_hilo_ordering_mode(void) {
  enum_result_t result;
  int err;

  memset(&result, 0, sizeof(result));
  err = enumResultAlloc(&result, 2, enum_ordering_mode_hilo);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_NOT_NULL(result.ordering);
  TEST_ASSERT_EQUAL_INT(enum_ordering_mode_hilo, result.ordering->mode);
  enumResultFree(&result);
}

/* Test enumResultPrint with Hi-only game */
static void test_result_print_hi_game(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* AA vs KK with flop */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  err = enumResultAlloc(&result, 2, enum_ordering_mode_hi);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_holdem, pockets, board, dead, 2, 5, 1, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* Should produce output without crashing */
  enumResultPrint(&result, pockets, board);
  enumResultPrintTerse(&result, pockets, board);

  enumResultFree(&result);
}

/* Test enumResultPrint with Hi/Lo game - must exercise hilo ordering code */
static void test_result_print_hilo_game(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Omaha8 setup - player 1 has good low draw */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);
  add_card(&pockets[0], StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  err = enumResultAlloc(&result, 2, enum_ordering_mode_hilo);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_omaha8, pockets, board, dead, 2, 5, 1, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* Exercise Hi/Lo print path */
  enumResultPrint(&result, pockets, board);
  enumResultPrintTerse(&result, pockets, board);

  enumResultFree(&result);
}

/* Test enumResultPrint with Lo-only game (Razz) - uses Monte Carlo for speed */
static void test_result_print_lo_game(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* Razz: Strong low vs weak low */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_2, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_3, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);

  memset(&result, 0, sizeof(result));
  /* Use Monte Carlo sampling (1000 samples) instead of exhaustive for speed */
  err = enumSample(game_razz, pockets, board, dead, 2, 3, 1000, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* Player 0 with A23 should dominate over KQJ */
  TEST_ASSERT_TRUE(result.ev[0] > result.ev[1]);

  /* Exercise Lo print path */
  enumResultPrint(&result, pockets, board);
  enumResultPrintTerse(&result, pockets, board);

  enumResultFree(&result);
}

/* Test error handling: too many players */
static void test_error_too_many_players(void) {
  StdDeck_CardMask pockets[ENUM_MAXPLAYERS + 1], board, dead;
  enum_result_t result;
  int err;

  /* Initialize pockets */
  for (int i = 0; i <= ENUM_MAXPLAYERS; i++) {
    StdDeck_CardMask_RESET(pockets[i]);
  }
  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));

  /* Should fail with too many players */
  err = enumExhaustive(game_holdem, pockets, board, dead, ENUM_MAXPLAYERS + 1,
                       0, 0, &result);
  TEST_ASSERT_EQUAL_INT(1, err);
}

/* Test Monte Carlo with 0 iterations should still work */
static void test_sample_zero_iterations(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  memset(&result, 0, sizeof(result));
  err = enumSample(game_holdem, pockets, board, dead, 2, 0, 0, 0, &result);

  /* With 0 iterations, should get 0 samples */
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(0, result.nsamples);

  enumResultFree(&result);
}

/* Test Short Deck Holdem enumeration */
static void test_short_deck_holdem(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* AA vs KK in short deck */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

  /* Board with cards 6+ only (short deck valid) */
  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_8, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  err = enumResultAlloc(&result, 2, enum_ordering_mode_hi);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_sdholdem, pockets, board, dead, 2, 5, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_TRUE(result.nsamples > 0);

  enumResultFree(&result);
}

/* Test 3-player ordering */
static void test_three_player_ordering(void) {
  StdDeck_CardMask pockets[3], board, dead;
  enum_result_t result;
  int err;

  /* AA vs KK vs QQ */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(pockets[2]);
  add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);
  add_card(&pockets[2], StdDeck_Rank_QUEEN, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
  add_card(&board, StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&board, StdDeck_Rank_9, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
  add_card(&board, StdDeck_Rank_JACK, StdDeck_Suit_HEARTS);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, pockets[2]);
  StdDeck_CardMask_OR(dead, dead, board);

  err = enumResultAlloc(&result, 3, enum_ordering_mode_hi);
  TEST_ASSERT_EQUAL_INT(0, err);

  err = enumExhaustive(game_holdem, pockets, board, dead, 3, 5, 1, &result);
  TEST_ASSERT_EQUAL_INT(0, err);

  /* Ordering should exist */
  TEST_ASSERT_NOT_NULL(result.ordering);

  /* Print with 3 players */
  enumResultPrint(&result, pockets, board);

  enumResultFree(&result);
}

/* Test enumSample with Omaha5 */
static void test_omaha5_sample(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;
  int err;

  /* 5-card Omaha hands */
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
  add_card(&pockets[0], StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_JACK, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_TEN, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_CLUBS);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(board);
  add_card(&board, StdDeck_Rank_6, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_5, StdDeck_Suit_DIAMONDS);
  add_card(&board, StdDeck_Rank_4, StdDeck_Suit_DIAMONDS);

  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_OR(dead, dead, pockets[0]);
  StdDeck_CardMask_OR(dead, dead, pockets[1]);
  StdDeck_CardMask_OR(dead, dead, board);

  memset(&result, 0, sizeof(result));
  err = enumSample(game_omaha5, pockets, board, dead, 2, 3, 1000, 0, &result);
  TEST_ASSERT_EQUAL_INT(0, err);
  TEST_ASSERT_EQUAL_INT(1000, result.nsamples);

  enumResultFree(&result);
}

/* Test invalidgame in enumResultPrint */
static void test_print_invalid_game(void) {
  StdDeck_CardMask pockets[2], board;
  enum_result_t result;

  StdDeck_CardMask_RESET(pockets[0]);
  StdDeck_CardMask_RESET(pockets[1]);
  StdDeck_CardMask_RESET(board);

  memset(&result, 0, sizeof(result));
  result.game = (enum_game_t)-1; /* Invalid game */
  result.nplayers = 2;
  result.nsamples = 1;

  /* Should handle gracefully - just print error message */
  enumResultPrint(&result, pockets, board);

  /* No crash means success */
  TEST_PASS();
}

int main(void) {
  UNITY_BEGIN();

  /* Game params tests */
  RUN_TEST(test_all_game_params);
  RUN_TEST(test_invalid_game_params);

  /* Result allocation tests */
  RUN_TEST(test_result_alloc_max_players);
  RUN_TEST(test_hilo_ordering_mode);

  /* Print function tests */
  RUN_TEST(test_result_print_hi_game);
  RUN_TEST(test_result_print_hilo_game);
  RUN_TEST(test_result_print_lo_game);
  RUN_TEST(test_three_player_ordering);
  RUN_TEST(test_print_invalid_game);

  /* Error handling tests */
  RUN_TEST(test_error_too_many_players);
  RUN_TEST(test_sample_zero_iterations);

  /* Game-specific tests */
  RUN_TEST(test_short_deck_holdem);
  RUN_TEST(test_omaha5_sample);

  return UNITY_END();
}
