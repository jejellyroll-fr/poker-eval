/*
 * test_fusion_equity.c -- Fusion equity enumeration (issue #295)
 *
 * Fusion starts as Hold'em and ends as Omaha Hi: a player holds two hole cards
 * preflop, three on the flop and four from the turn on, the showdown uses
 * exactly two hole cards with three board cards, and a player who is still owed
 * hole cards receives them as the hand goes on.  An equity trial therefore has
 * to deal future hole cards as well as the missing board cards.
 *
 * These tests pin that behaviour, and the places where it must differ from the
 * two games it borrows from:
 *
 *   1. Turn and river all-ins agree with omaha, card for card.
 *   2. Four spades on the board with one spade in hand is a flush in Hold'em
 *      and is not a flush in Fusion.
 *   3. A flop all-in deals the future hole cards: the number of deals matches
 *      the combinations that are actually available, sampling converges on the
 *      enumeration, and more hole cards than the stage allows are rejected.
 *   4. A preflop all-in, which cannot be enumerated, still samples.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/games/eval_fusion.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/rules_std.h>

static int failures = 0;

#define CHECK(cond, ...)                                                       \
  do                                                                           \
  {                                                                            \
    if (!(cond))                                                               \
    {                                                                          \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                     \
      fprintf(stderr, __VA_ARGS__);                                            \
      fprintf(stderr, "\n");                                                   \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static StdDeck_CardMask mask_of(const char *cards)
{
  StdDeck_CardMask m;
  char buf[3] = {0, 0, 0};
  size_t i;

  StdDeck_CardMask_RESET(m);
  for (i = 0; cards[i] != '\0' && cards[i + 1] != '\0'; i += 2)
  {
    int card;
    buf[0] = cards[i];
    buf[1] = cards[i + 1];
    if (StdDeck_stringToCard(buf, &card) > 0)
      StdDeck_CardMask_SET(m, card);
  }
  return m;
}

/* Marks every card outside `used` as dead except `live` of them, so an
 * exhaustive space that would otherwise be huge stays enumerable. */
static StdDeck_CardMask dead_leaving(StdDeck_CardMask used, int live)
{
  StdDeck_CardMask dead;
  int card, kept = 0;

  StdDeck_CardMask_RESET(dead);
  for (card = 0; card < StdDeck_N_CARDS; ++card)
  {
    if (StdDeck_CardMask_CARD_IS_SET(used, card))
      continue;
    if (kept < live)
      kept++;
    else
      StdDeck_CardMask_SET(dead, card);
  }
  return dead;
}

static double equity_of(const enum_result_t *result, int player)
{
  if (result->nsamples == 0)
    return -1.0;
  return result->ev[player] / result->nsamples;
}

/* 1. Turn and river all-ins: every player already holds four hole cards, so
 * Fusion must reproduce omaha exactly.  The turn additionally deals one river
 * card. */
static void compare_fusion_with_omaha(const char *hole0, const char *hole1,
                                      const char *board_cards, const char *label)
{
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t fusion, omaha;
  int i;

  pockets[0] = mask_of(hole0);
  pockets[1] = mask_of(hole1);
  board = mask_of(board_cards);
  StdDeck_CardMask_RESET(dead);
  memset(&fusion, 0, sizeof(fusion));
  memset(&omaha, 0, sizeof(omaha));

  CHECK(enumExhaustive(game_fusion, pockets, board, dead, 2,
                       StdDeck_numCards(board), 0, &fusion) == 0,
        "%s: fusion enumeration failed", label);
  CHECK(enumExhaustive(game_omaha, pockets, board, dead, 2,
                       StdDeck_numCards(board), 0, &omaha) == 0,
        "%s: omaha enumeration failed", label);

  CHECK(fusion.nsamples == omaha.nsamples, "%s: %u samples, omaha %u", label,
        fusion.nsamples, omaha.nsamples);
  for (i = 0; i < 2; i++)
  {
    CHECK(fusion.nwinhi[i] == omaha.nwinhi[i], "%s: player %d wins %u, omaha %u",
          label, i, fusion.nwinhi[i], omaha.nwinhi[i]);
    CHECK(fusion.ntiehi[i] == omaha.ntiehi[i], "%s: player %d ties %u, omaha %u",
          label, i, fusion.ntiehi[i], omaha.ntiehi[i]);
    CHECK(fabs(fusion.ev[i] - omaha.ev[i]) < 1e-12,
          "%s: player %d ev %f, omaha %f", label, i, fusion.ev[i], omaha.ev[i]);
  }
  enumResultFree(&fusion);
  enumResultFree(&omaha);
}

static void test_river_matches_omaha(void)
{
  compare_fusion_with_omaha("AsKsQdJc", "9h9d8c7c", "2h3hTs4d5s", "river");
}

static void test_turn_matches_omaha(void)
{
  compare_fusion_with_omaha("AsKsQdJc", "9h9d8c7c", "2h3hTs4d", "turn");
}

/* 2. The Hold'em/Fusion disagreement the issue calls out: the board holds four
 * spades and the player holds one, which is a flush in Hold'em (five spades)
 * but not here, because Fusion needs two hole cards of the suit. */
static void test_four_spades_on_board_is_not_a_flush(void)
{
  StdDeck_CardMask hole = mask_of("AsKdQcJh");
  StdDeck_CardMask holdem_hole = mask_of("AsKd");
  StdDeck_CardMask board = mask_of("2s5s9sKs3d");
  StdDeck_CardMask holdem_hand;
  HandVal fusion_val = HandVal_NOTHING;
  HandVal holdem_val;

  CHECK(StdDeck_Fusion_EVAL(hole, board, &fusion_val) == 0,
        "fusion evaluation failed");
  CHECK(HandVal_HANDTYPE(fusion_val) != StdRules_HandType_FLUSH,
        "four board spades plus one hole spade must not be a flush in fusion");
  CHECK(HandVal_HANDTYPE(fusion_val) == StdRules_HandType_ONEPAIR,
        "best fusion hand is a pair of kings, got hand type %u",
        (unsigned)HandVal_HANDTYPE(fusion_val));

  StdDeck_CardMask_OR(holdem_hand, holdem_hole, board);
  holdem_val = StdDeck_StdRules_EVAL_N(holdem_hand, 7);
  CHECK(HandVal_HANDTYPE(holdem_val) == StdRules_HandType_FLUSH,
        "the same cards are a flush in hold'em, got hand type %u",
        (unsigned)HandVal_HANDTYPE(holdem_val));
}

/* 3. A flop all-in: both players hold two cards, the board shows three, so each
 * trial deals two board cards, both players' turn card and both players' river
 * card, and the showdown is Omaha Hi over 4 hole + 5 board cards.
 *
 * Most of the deck is dead here so the exhaustive space is enumerable: ten live
 * cards leave C(10,2) * C(8,2) * C(6,2) deals.  A card dealt to two places, or
 * a player left one card short, would change either that count or the result of
 * the evaluation. */
static void test_flop_all_in_deals_future_hole_cards(void)
{
  StdDeck_CardMask pockets[2], board, dead, used;
  enum_result_t exhaustive, sampled;
  const int iterations = 20000;
  const unsigned expected_deals = 18900; /* 45 * 28 * 15 */
  int i;

  pockets[0] = mask_of("AsAd");
  pockets[1] = mask_of("KhKc");
  board = mask_of("2s5s9d");
  StdDeck_CardMask_RESET(used);
  StdDeck_CardMask_OR(used, pockets[0], pockets[1]);
  StdDeck_CardMask_OR(used, used, board);
  dead = dead_leaving(used, 10);
  memset(&exhaustive, 0, sizeof(exhaustive));
  memset(&sampled, 0, sizeof(sampled));

  CHECK(enumExhaustive(game_fusion, pockets, board, dead, 2, 3, 0,
                       &exhaustive) == 0,
        "flop fusion enumeration failed");
  CHECK(exhaustive.nsamples == expected_deals,
        "flop enumerated %u deals, expected %u", exhaustive.nsamples,
        expected_deals);
  CHECK(fabs(equity_of(&exhaustive, 0) + equity_of(&exhaustive, 1) - 1.0) < 1e-9,
        "equities sum to %f",
        equity_of(&exhaustive, 0) + equity_of(&exhaustive, 1));
  CHECK(equity_of(&exhaustive, 0) > 0.0 && equity_of(&exhaustive, 1) > 0.0,
        "both players must win some share: %f / %f", equity_of(&exhaustive, 0),
        equity_of(&exhaustive, 1));

  /* Sampling the same spot uses its own dealing code (random draws instead of
     the enumeration) and must land on the same equities. */
  CHECK(enumSample(game_fusion, pockets, board, dead, 2, 3, iterations, 0,
                   &sampled) == 0,
        "flop fusion sampling failed");
  CHECK(sampled.nsamples == (unsigned)iterations, "sampled %u of %d trials",
        sampled.nsamples, iterations);
  for (i = 0; i < 2; i++)
    CHECK(fabs(equity_of(&exhaustive, i) - equity_of(&sampled, i)) < 0.02,
          "player %d: enumerated %f, sampled %f", i, equity_of(&exhaustive, i),
          equity_of(&sampled, i));

  enumResultFree(&exhaustive);
  enumResultFree(&sampled);
}

/* A player cannot hold more cards than the stage dealt him. */
static void test_stage_card_count_is_enforced(void)
{
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  pockets[0] = mask_of("AsAdKsQd"); /* four on the flop: impossible */
  pockets[1] = mask_of("KhKc");
  board = mask_of("2s5s9d");
  StdDeck_CardMask_RESET(dead);
  memset(&result, 0, sizeof(result));

  CHECK(enumExhaustive(game_fusion, pockets, board, dead, 2, 3, 0, &result) != 0,
        "four hole cards on the flop must be rejected");
  enumResultFree(&result);
}

/* 4. Preflop both players are still owed two hole cards on top of the five
 * board cards, which is far too large to enumerate: sampling has to handle it.
 * The equities must sum to one and a second run must land in the same place. */
static void test_preflop_sampling_deals_future_hole_cards(void)
{
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t first, second;
  const int iterations = 20000;

  pockets[0] = mask_of("AsAd");
  pockets[1] = mask_of("KhKc");
  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));

  CHECK(enumSample(game_fusion, pockets, board, dead, 2, 0, iterations, 0,
                   &first) == 0,
        "preflop fusion sampling failed");
  CHECK(enumSample(game_fusion, pockets, board, dead, 2, 0, iterations, 0,
                   &second) == 0,
        "second preflop fusion sampling failed");

  CHECK(first.nsamples == (unsigned)iterations, "sampled %u of %d trials",
        first.nsamples, iterations);
  CHECK(fabs(equity_of(&first, 0) + equity_of(&first, 1) - 1.0) < 1e-9,
        "equities sum to %f", equity_of(&first, 0) + equity_of(&first, 1));
  CHECK(equity_of(&first, 0) > 0.52, "aces should lead kings preflop, got %f",
        equity_of(&first, 0));
  CHECK(fabs(equity_of(&first, 0) - equity_of(&second, 0)) < 0.02,
        "two runs differ: %f vs %f", equity_of(&first, 0),
        equity_of(&second, 0));

  enumResultFree(&first);
  enumResultFree(&second);
}

int main(void)
{
  test_river_matches_omaha();
  test_turn_matches_omaha();
  test_four_spades_on_board_is_not_a_flush();
  test_flop_all_in_deals_future_hole_cards();
  test_stage_card_count_is_enforced();
  test_preflop_sampling_deals_future_hole_cards();

  if (failures != 0)
  {
    fprintf(stderr, "%d fusion equity check(s) failed\n", failures);
    return 1;
  }
  printf("test_fusion_equity: all checks passed\n");
  return 0;
}
