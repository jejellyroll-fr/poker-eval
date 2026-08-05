/**
 * test_montecarlo_convergence_unity.c
 *
 * enumSample() must converge to enumExhaustive() on any spot small enough to
 * enumerate. That property held for no board game at all until the dead-card
 * fix: enumSample computed `effective_dead` (the caller's dead cards plus the
 * board plus every player's hole cards) and then handed the sampler plain
 * `dead`, so it redealt cards that were already in play.
 *
 * The symptom depended on how strict each evaluator was. Hold'em silently
 * overstated equity by ~2.5 points; Omaha and Courchevel returned error 1002
 * and Pineapple 1001, because their evaluators reject a board that is short a
 * card. No test compared the two paths, which is why it went unnoticed.
 */

#include "unity.h"
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Monte Carlo draws needed to keep the tolerance below meaningful. */
#define MC_ITERATIONS 400000
/* Sampling noise at 400k draws is well under a tenth of a point; 0.6 leaves
   generous headroom while still catching the 1.4-2.6 point bias the bug
   produced. */
#define MC_TOLERANCE 0.006

static StdDeck_CardMask mask_of(const char *cards) {
  StdDeck_CardMask m;
  char buf[3] = {0, 0, 0};
  StdDeck_CardMask_RESET(m);
  for (size_t i = 0; cards[i] && cards[i + 1]; i += 2) {
    int card;
    buf[0] = cards[i];
    buf[1] = cards[i + 1];
    if (StdDeck_stringToCard(buf, &card) > 0)
      StdDeck_CardMask_SET(m, card);
  }
  return m;
}

static double equity_of(const enum_result_t *r, int player) {
  if (r->nsamples == 0)
    return -1.0;
  return (r->nwinhi[player] + 0.5 * r->ntiehi[player]) / (double)r->nsamples;
}

static void assert_converges(enum_game_t game, const char *name,
                             const char *hand1, const char *hand2,
                             const char *board) {
  StdDeck_CardMask pockets[2], board_mask, dead;
  enum_result_t exhaustive, sampled;
  int nboard = (int)(strlen(board) / 2);
  char msg[160];

  pockets[0] = mask_of(hand1);
  pockets[1] = mask_of(hand2);
  board_mask = mask_of(board);
  StdDeck_CardMask_RESET(dead);
  memset(&exhaustive, 0, sizeof(exhaustive));
  memset(&sampled, 0, sizeof(sampled));

  snprintf(msg, sizeof(msg), "%s: enumExhaustive failed", name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0,
      enumExhaustive(game, pockets, board_mask, dead, 2, nboard, 0, &exhaustive),
      msg);

  /* Before the fix this returned 1001/1002 for several of these games. */
  snprintf(msg, sizeof(msg), "%s: enumSample failed", name);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0,
      enumSample(game, pockets, board_mask, dead, 2, nboard, MC_ITERATIONS, 0,
                 &sampled),
      msg);

  TEST_ASSERT_TRUE(sampled.nsamples > 0);

  for (int player = 0; player < 2; ++player) {
    snprintf(msg, sizeof(msg),
             "%s: player %d sampled %.4f vs exhaustive %.4f", name, player,
             equity_of(&sampled, player), equity_of(&exhaustive, player));
    TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(MC_TOLERANCE, equity_of(&exhaustive, player),
                                      equity_of(&sampled, player), msg);
  }

  enumResultFree(&exhaustive);
  enumResultFree(&sampled);
}

static void test_holdem_converges(void) {
  assert_converges(game_holdem, "holdem", "AsAd", "7h8h", "2s5s9h");
  assert_converges(game_holdem8, "holdem8", "AsAd", "7h8h", "2s5s9h");
}

static void test_omaha_converges(void) {
  assert_converges(game_omaha, "omaha", "AsAdKsQd", "7h8h9c6c", "2s5s9d");
  assert_converges(game_omaha8, "omaha8", "AsAdKsQd", "7h8h9c6c", "2s5s9d");
}

static void test_pineapple_converges(void) {
  assert_converges(game_pineapple, "pineapple", "AsAdKs", "7h8h9c", "2s5s9d");
  assert_converges(game_pineapple8, "pineapple8", "AsAdKs", "7h8h9c", "2s5s9d");
  assert_converges(game_pineapple_lazy, "pineapple lazy", "AsAdKs", "7h8h9c",
                   "2s5s9d");
  assert_converges(game_pineapple_crazy, "pineapple crazy", "AsAdKs", "7h8h9c",
                   "2s5s9d");
}

static void test_courchevel_converges(void) {
  assert_converges(game_courchevel, "courchevel", "AsAdKsQdJd", "7h8h9c6c5c",
                   "2s5s9d");
}

/* Preflop exercises the five-card sampling path rather than the two-card one. */
static void test_preflop_converges(void) {
  assert_converges(game_holdem, "holdem preflop", "AsAd", "7h8h", "");
  assert_converges(game_omaha, "omaha preflop", "AsAdKsQd", "7h8h9c6c", "");
}

/* The sampled board must never collide with a hole card or a board card. This
   is the defect itself rather than one of its symptoms, so check it directly:
   every sampled showdown has to involve exactly the expected card count. */
static void test_no_duplicate_cards_dealt(void) {
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t sampled;

  pockets[0] = mask_of("AsAdKs");
  pockets[1] = mask_of("7h8h9c");
  board = mask_of("2s5s9d");
  StdDeck_CardMask_RESET(dead);
  memset(&sampled, 0, sizeof(sampled));

  /* evaluate_best_two_hole_holdem insists on a five-card board, so a single
     redealt card surfaces as INNER_LOOP's 1000 + err rather than as a quietly
     wrong number. Before the fix this returned 1001 on the first iteration. */
  TEST_ASSERT_EQUAL_INT(0, enumSample(game_pineapple, pockets, board, dead, 2, 3,
                                      MC_ITERATIONS, 0, &sampled));
  TEST_ASSERT_EQUAL_UINT32(MC_ITERATIONS, sampled.nsamples);
  enumResultFree(&sampled);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_holdem_converges);
  RUN_TEST(test_omaha_converges);
  RUN_TEST(test_pineapple_converges);
  RUN_TEST(test_courchevel_converges);
  RUN_TEST(test_preflop_converges);
  RUN_TEST(test_no_duplicate_cards_dealt);
  return UNITY_END();
}
