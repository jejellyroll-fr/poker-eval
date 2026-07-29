/*
 * eval_fusion.h -- Fusion poker hand evaluator
 *
 * Fusion is a hybrid of Hold'em and Omaha where:
 * - Players start with 2 hole cards (like Hold'em)
 * - Receive 3rd hole card after flop
 * - Receive 4th hole card after turn
 * - Must use exactly 2 hole cards + 3 board cards (like Omaha)
 * - Pot Limit betting structure
 */

#ifndef __EVAL_FUSION_H__
#define __EVAL_FUSION_H__

#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>

/* Fusion evaluation is identical to Omaha Hi evaluation
 * since both require exactly 2 hole cards + 3 board cards.
 * The difference is in card progression, not evaluation.
 */

/* Evaluate a Fusion hand for high only. Return nonzero on error. */
static inline int
StdDeck_Fusion_EVAL(StdDeck_CardMask hole, StdDeck_CardMask board,
                    HandVal *hival)
{
  /* Fusion uses exactly the same evaluation as Omaha Hi:
   * - Must use exactly 2 hole cards
   * - Must use exactly 3 board cards
   * - No low evaluation (Fusion is high-only)
   */
  return StdDeck_OmahaHi_EVAL(hole, board, hival);
}

/* Validate Fusion hand based on game stage */
static inline int
StdDeck_Fusion_ValidateHand(StdDeck_CardMask hole, int stage)
{
  int nhole = 0;
  int i;

  /* Count hole cards */
  for (i = 0; i < StdDeck_N_CARDS; i++)
  {
    if (StdDeck_CardMask_CARD_IS_SET(hole, i))
    {
      nhole++;
    }
  }

  /* Validate based on game stage:
   * Stage 0 (preflop): 2 cards
   * Stage 1 (flop): 3 cards
   * Stage 2 (turn): 4 cards
   * Stage 3 (river): 4 cards (no additional card)
   */
  switch (stage)
  {
  case 0:
    return (nhole == 2) ? 0 : 1; /* preflop: exactly 2 cards */
  case 1:
    return (nhole == 3) ? 0 : 1; /* flop: exactly 3 cards */
  case 2:
  case 3:
    return (nhole == 4) ? 0 : 1; /* turn/river: exactly 4 cards */
  default:
    return 1; /* invalid stage */
  }
}

#endif
