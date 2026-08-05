/**
 * test_pineapple_variants_unity.c
 *
 * Unity tests for the two Pineapple variants added alongside game_pineapple:
 *
 *  - game_pineapple_lazy  (Tahoe): all three hole cards reach showdown and the
 *    best two play. This is by construction the same evaluation as
 *    game_pineapple, and the tests below pin that equivalence so the two
 *    cannot drift apart.
 *
 *  - game_pineapple_crazy: the third card is discarded after the flop, so the
 *    decision may use the flop but not the turn or the river. The discard is
 *    committed once, then the surviving two cards play out as a Hold'em
 *    pocket. Without a flop there is nothing to decide on, so those states are
 *    rejected rather than silently evaluated with full-board knowledge.
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

/* P0: As Ad Ks   P1: 7h 8h 9c   flop: 2s 5s 9h
   P0 chooses between the aces and the As/Ks spade draw; P1 between a pair of
   nines and the heart/straight cards. Both decisions matter on this flop. */
static void deal_sample(StdDeck_CardMask pockets[2], StdDeck_CardMask *board,
                        int nboard) {
  StdDeck_CardMask_RESET(pockets[0]);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
  add_card(&pockets[0], StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS);
  add_card(&pockets[0], StdDeck_Rank_KING, StdDeck_Suit_SPADES);

  StdDeck_CardMask_RESET(pockets[1]);
  add_card(&pockets[1], StdDeck_Rank_7, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_8, StdDeck_Suit_HEARTS);
  add_card(&pockets[1], StdDeck_Rank_9, StdDeck_Suit_CLUBS);

  StdDeck_CardMask_RESET(*board);
  if (nboard >= 3) {
    add_card(board, StdDeck_Rank_2, StdDeck_Suit_SPADES);
    add_card(board, StdDeck_Rank_5, StdDeck_Suit_SPADES);
    add_card(board, StdDeck_Rank_9, StdDeck_Suit_HEARTS);
  }
  if (nboard >= 4)
    add_card(board, StdDeck_Rank_TEN, StdDeck_Suit_DIAMONDS);
  if (nboard >= 5)
    add_card(board, StdDeck_Rank_3, StdDeck_Suit_CLUBS);
}

static double equity_of(enum_result_t *r, int player) {
  if (r->nsamples == 0)
    return 0.0;
  return (r->nwinhi[player] + 0.5 * r->ntiehi[player]) / (double)r->nsamples;
}

static void test_variants_registered(void) {
  enum_gameparams_t *crazy = enumGameParams(game_pineapple_crazy);
  enum_gameparams_t *lazy = enumGameParams(game_pineapple_lazy);

  TEST_ASSERT_NOT_NULL(crazy);
  TEST_ASSERT_NOT_NULL(lazy);

  /* enum_gameparams[] is indexed by the enum, so a mismatch here means the
     table and the enum have drifted out of order. */
  TEST_ASSERT_EQUAL_INT(game_pineapple_crazy, crazy->game);
  TEST_ASSERT_EQUAL_INT(game_pineapple_lazy, lazy->game);

  TEST_ASSERT_EQUAL_INT(3, crazy->minpocket);
  TEST_ASSERT_EQUAL_INT(3, crazy->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, crazy->maxboard);
  TEST_ASSERT_EQUAL_INT(0, crazy->haslopot);
  TEST_ASSERT_EQUAL_INT(1, crazy->hashipot);

  TEST_ASSERT_EQUAL_INT(3, lazy->minpocket);
  TEST_ASSERT_EQUAL_INT(3, lazy->maxpocket);
  TEST_ASSERT_EQUAL_INT(5, lazy->maxboard);
  TEST_ASSERT_EQUAL_INT(0, lazy->haslopot);
  TEST_ASSERT_EQUAL_INT(1, lazy->hashipot);
}

/* Lazy is the existing pineapple rule under its own name: identical counts,
   not merely similar equities. */
static void test_lazy_matches_pineapple(void) {
  const int boards[3] = {0, 3, 5};

  for (int b = 0; b < 3; ++b) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t base, lazy;

    deal_sample(pockets, &board, boards[b]);
    StdDeck_CardMask_RESET(dead);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple, pockets, board, dead,
                                            2, boards[b], 0, &base));
    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_lazy, pockets, board,
                                            dead, 2, boards[b], 0, &lazy));

    TEST_ASSERT_EQUAL_UINT32(base.nsamples, lazy.nsamples);
    for (int p = 0; p < 2; ++p) {
      TEST_ASSERT_EQUAL_UINT32(base.nwinhi[p], lazy.nwinhi[p]);
      TEST_ASSERT_EQUAL_UINT32(base.ntiehi[p], lazy.ntiehi[p]);
      TEST_ASSERT_EQUAL_UINT32(base.nlosehi[p], lazy.nlosehi[p]);
    }

    enumResultFree(&base);
    enumResultFree(&lazy);
  }
}

/* Committing on the flop is a different game from choosing against the final
   board, so the equities must actually differ on a flop where the choice is
   live. Equities must still be a zero-sum split. */
static void test_crazy_differs_from_lazy_on_the_flop(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t lazy, crazy;
  double lazy0, crazy0, lazy1, crazy1;

  deal_sample(pockets, &board, 3);
  StdDeck_CardMask_RESET(dead);

  TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_lazy, pockets, board,
                                          dead, 2, 3, 0, &lazy));
  TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_crazy, pockets, board,
                                          dead, 2, 3, 0, &crazy));

  /* Same runout space either way. */
  TEST_ASSERT_EQUAL_UINT32(lazy.nsamples, crazy.nsamples);

  lazy0 = equity_of(&lazy, 0);
  lazy1 = equity_of(&lazy, 1);
  crazy0 = equity_of(&crazy, 0);
  crazy1 = equity_of(&crazy, 1);

  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, lazy0 + lazy1);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, crazy0 + crazy1);

  /* The whole point of the variant: losing turn/river information changes the
     result. */
  TEST_ASSERT_TRUE_MESSAGE(crazy0 < lazy0 - 1e-6 || crazy0 > lazy0 + 1e-6,
                           "crazy pineapple must not equal the lazy rule here");

  enumResultFree(&lazy);
  enumResultFree(&crazy);
}

/* Without a flop the discard has nothing to act on. Evaluating it anyway would
   hand the player information they never had, so those states are refused. */
static void test_crazy_requires_a_flop(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  deal_sample(pockets, &board, 0);
  StdDeck_CardMask_RESET(dead);

  TEST_ASSERT_NOT_EQUAL_INT(0, enumExhaustive(game_pineapple_crazy, pockets,
                                              board, dead, 2, 0, 0, &result));

  /* The lazy rule has no such restriction. */
  TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_lazy, pockets, board,
                                          dead, 2, 0, 0, &result));
  enumResultFree(&result);
}

/* Turn and river states are supported: the flop still drives the decision. */
static void test_crazy_accepts_turn_and_river(void) {
  for (int nboard = 4; nboard <= 5; ++nboard) {
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t result;

    deal_sample(pockets, &board, nboard);
    StdDeck_CardMask_RESET(dead);

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_crazy, pockets, board,
                                            dead, 2, nboard, 0, &result));
    TEST_ASSERT_TRUE(result.nsamples > 0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0,
                              equity_of(&result, 0) + equity_of(&result, 1));
    enumResultFree(&result);
  }
}

/* The Monte Carlo path must accept both variants and land near the exhaustive
   answer. */
static void test_monte_carlo_paths(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t exhaustive, sampled;

  deal_sample(pockets, &board, 3);
  StdDeck_CardMask_RESET(dead);

  for (int variant = 0; variant < 2; ++variant) {
    enum_game_t game = variant ? game_pineapple_crazy : game_pineapple_lazy;

    TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game, pockets, board, dead, 2, 3, 0,
                                            &exhaustive));
    TEST_ASSERT_EQUAL_INT(0, enumSample(game, pockets, board, dead, 2, 3, 200000,
                                        0, &sampled));

    TEST_ASSERT_TRUE(sampled.nsamples > 0);
    /* 200k samples on a 903-board space: a 2 point window is generous. */
    TEST_ASSERT_DOUBLE_WITHIN(0.02, equity_of(&exhaustive, 0),
                              equity_of(&sampled, 0));

    enumResultFree(&exhaustive);
    enumResultFree(&sampled);
  }
}

/* With ordering enabled the game must be routed to a hi-only ordering mode
   rather than falling into the switch default, which returns an error. */
static void test_ordering_supported(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  deal_sample(pockets, &board, 3);
  StdDeck_CardMask_RESET(dead);

  TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_lazy, pockets, board,
                                          dead, 2, 3, 1, &result));
  enumResultFree(&result);

  TEST_ASSERT_EQUAL_INT(0, enumExhaustive(game_pineapple_crazy, pockets, board,
                                          dead, 2, 3, 1, &result));
  enumResultFree(&result);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_variants_registered);
  RUN_TEST(test_lazy_matches_pineapple);
  RUN_TEST(test_crazy_differs_from_lazy_on_the_flop);
  RUN_TEST(test_crazy_requires_a_flop);
  RUN_TEST(test_crazy_accepts_turn_and_river);
  RUN_TEST(test_monte_carlo_paths);
  RUN_TEST(test_ordering_supported);
  return UNITY_END();
}
