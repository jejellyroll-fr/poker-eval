/**
 * test_pineapple_batched_unity.c
 *
 * Unity tests for the batched Monte-Carlo evaluators that back the Pineapple
 * family (game_pineapple, game_pineapple_lazy, game_pineapple8 and
 * game_pineapple_crazy).
 *
 * The batched engine (enumSampleBatched) used to fall back to the scalar
 * enumSample for these games; the pineapple evaluators registered in
 * batched_montecarlo.c now evaluate whole BoardBatchs at once, optionally
 * through the SIMD kernels. These tests pin the batched answers against the
 * scalar reference implementation (and, where cheap, the exhaustive one).
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/equity/simd_operations.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static void add_card(StdDeck_CardMask *mask, int rank, int suit) {
  StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(rank, suit));
}

/* P0: As Ad Ks   P1: 7h 8h 9c   flop: 2s 5s 9h
   P0 can play the two aces, the As/Ks spade draw or a bare cowboys hand;
   P1 a pair of nines or the heart/straight cards. Every choice matters here. */
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

/* Overall pot-share equity, including low splits, from result->ev which both
   engines accumulate. */
static double total_equity(enum_result_t *r, int player) {
  if (r->nsamples == 0)
    return 0.0;
  return r->ev[player] / (double)r->nsamples;
}

/* Exercise every short SIMD batch size. In particular, two players produce
 * six Pineapple candidates, which used to make the AVX2 low-8 implementation
 * store four lanes past its eight-element result buffer. */
static void test_simd_candidate_batch_sizes_match_scalar(void) {
  StdDeck_CardMask hands[9];
  HandVal high[9];
  LowHandVal low[9];

  for (int h = 0; h < 9; ++h) {
    StdDeck_CardMask_RESET(hands[h]);
    for (int c = 0; c < 7; ++c) {
      int rank = (h + c) % 13;
      int suit = (h * 3 + c) % 4;
      add_card(&hands[h], rank, suit);
    }
  }

  for (int count = 1; count <= 9; ++count) {
    TEST_ASSERT_EQUAL_INT(0, simd_eval_multiple_hands(hands, count, high));
    TEST_ASSERT_EQUAL_INT(0, simd_eval_low8_multiple_hands(hands, count, low));
    for (int i = 0; i < count; ++i) {
      TEST_ASSERT_EQUAL_UINT(StdDeck_StdRules_EVAL_N_Cached(hands[i], 7),
                             high[i]);
      TEST_ASSERT_EQUAL_UINT(pe_eval_low_a5(hands[i]), low[i]);
    }
  }
}

/* Confirm the registered batched evaluators land on the same equities as the
 * scalar reference (enumSample) for every Pineapple variant. */
static void test_batched_matches_legacy(void) {
  const int variants[3] = {game_pineapple, game_pineapple_lazy,
                           game_pineapple8};

  for (int v = 0; v < 3; ++v) {
    enum_game_t game = variants[v];
    StdDeck_CardMask pockets[2], board, dead;
    enum_result_t legacy, batched;

    deal_sample(pockets, &board, 3);
    StdDeck_CardMask_RESET(dead);

    TEST_ASSERT_EQUAL_INT(0,
                          enumSample(game, pockets, board, dead, 2, 3, 200000,
                                     0, &legacy));
    TEST_ASSERT_EQUAL_INT(0,
                          enumSampleBatched(game, pockets, board, dead, 2, 3,
                                            200000, 0, &batched));

    TEST_ASSERT_TRUE(batched.nsamples > 0);
    /* 200k samples over a C(47,2) runout space: a 2 point window is ample. */
    TEST_ASSERT_DOUBLE_WITHIN(0.02, total_equity(&legacy, 0),
                              total_equity(&batched, 0));
    TEST_ASSERT_DOUBLE_WITHIN(0.02, total_equity(&legacy, 1),
                              total_equity(&batched, 1));
    /* The pot still closes. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                              total_equity(&batched, 0) + total_equity(&batched, 1));

    enumResultFree(&legacy);
    enumResultFree(&batched);
  }
}

/* The hi/lo split of pineapple8 is batched against the scalar one: the pot
 * share (result->ev) and the individual win tallies must agree inside sampling
 * noise, so the low pot cannot have been dropped by the batched path. */
static void test_pineapple8_hilo_split_matches(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t legacy, batched;

  deal_sample(pockets, &board, 3);
  StdDeck_CardMask_RESET(dead);

  TEST_ASSERT_EQUAL_INT(0,
                        enumSample(game_pineapple8, pockets, board, dead, 2, 3,
                                   400000, 0, &legacy));
  TEST_ASSERT_EQUAL_INT(0,
                        enumSampleBatched(game_pineapple8, pockets, board, dead,
                                          2, 3, 400000, 0, &batched));

  for (int p = 0; p < 2; ++p) {
    double leq = total_equity(&legacy, p);
    double beq = total_equity(&batched, p);

    TEST_ASSERT_DOUBLE_WITHIN(0.02, leq, beq);
    TEST_ASSERT_DOUBLE_WITHIN(0.02, legacy.nwinlo[p] / (double)legacy.nsamples,
                              batched.nwinlo[p] / (double)batched.nsamples);
  }

  enumResultFree(&legacy);
  enumResultFree(&batched);
}

/* Crazy Pineapple commits its discard at the flop, once, and the two
 * survivors then play as a Hold'em pocket. The batched engine must reproduce
 * the legacy commit (same flop, same dead) and therefore the same equity
 * distribution. */
static void test_crazy_batched_matches_legacy(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t legacy, batched;

  deal_sample(pockets, &board, 3);
  StdDeck_CardMask_RESET(dead);

  TEST_ASSERT_EQUAL_INT(0,
                        enumSample(game_pineapple_crazy, pockets, board, dead,
                                   2, 3, 200000, 0, &legacy));
  TEST_ASSERT_EQUAL_INT(0,
                        enumSampleBatched(game_pineapple_crazy, pockets, board,
                                          dead, 2, 3, 200000, 0, &batched));

  TEST_ASSERT_TRUE(batched.nsamples > 0);
  TEST_ASSERT_DOUBLE_WITHIN(0.02, total_equity(&legacy, 0),
                            total_equity(&batched, 0));
  TEST_ASSERT_DOUBLE_WITHIN(0.02, total_equity(&legacy, 1),
                            total_equity(&batched, 1));
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                            total_equity(&batched, 0) +
                            total_equity(&batched, 1));

  enumResultFree(&legacy);
  enumResultFree(&batched);
}

/* Without a flop there is nothing to commit on; the batched engine must
 * refuse such states just like the classic sample path does. */
static void test_crazy_batched_requires_a_flop(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  for (int nboard = 0; nboard < 3; ++nboard) {
    deal_sample(pockets, &board, nboard);
    StdDeck_CardMask_RESET(dead);
    TEST_ASSERT_NOT_EQUAL_INT(0,
                              enumSampleBatched(game_pineapple_crazy, pockets,
                                                board, dead, 2, nboard, 1000, 0,
                                                &result));
  }

  /* From the flop on, batched evaluation is accepted. */
  deal_sample(pockets, &board, 3);
  TEST_ASSERT_EQUAL_INT(0,
                        enumSampleBatched(game_pineapple_crazy, pockets, board,
                                          dead, 2, 3, 5000, 0, &result));
  TEST_ASSERT_TRUE(result.nsamples > 0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1.0,
                            total_equity(&result, 0) +
                            total_equity(&result, 1));
  enumResultFree(&result);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simd_candidate_batch_sizes_match_scalar);
  RUN_TEST(test_batched_matches_legacy);
  RUN_TEST(test_pineapple8_hilo_split_matches);
  RUN_TEST(test_crazy_batched_matches_legacy);
  RUN_TEST(test_crazy_batched_requires_a_flop);
  return UNITY_END();
}
