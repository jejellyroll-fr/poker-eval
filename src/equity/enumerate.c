/* enumerate.c -- functions to compute pot equity by enumerating outcomes
  Exports:
        enumExhaustive()	exhaustive enumeration of outcomes
        enumGameParams()	look up rule parameters by game type
        enumResultAlloc()	allocate ordering histograms in result object
        enumResultClear()	clear enumeration result object
        enumResultFree()	free ordering histograms in result object
        enumResultPrint()	print enumeration result object
        enumResultPrintTerse()	print enumeration result object, tersely
        enumSample()		monte carlo sampling of outcomes

   Copyright (C) Apr 2002, Michael Maurer.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low8.h>
#include <poker_eval/games/eval_low27.h>
#include <poker_eval/games/eval_joker_low.h>
#include <poker_eval/games/eval_joker_low8.h>
#include <poker_eval/games/eval_joker.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/eval_drawmaha.h>
#include <poker_eval/games/eval_pineapple.h>
#include <poker_eval/games/badugi_eval.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>

#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/universal_deck.h> // For Universal_ConvertStdToJoker
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/games/rules_short.h>
#include <poker_eval/games/eval_short.h>
#include <poker_eval/core/enumeration_adapters.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/utils/micro_optimizations.h>
#include <poker_eval/core/eval_cache.h>

static inline LowHandVal apply_low_qualifier(LowHandVal value, low_qualifier_t qualifier)
{
  if (value == LowHandVal_NOTHING || qualifier == LOW_QUALIFIER_NONE)
    return value;
  return pe_low_qualify5(value, qualifier) ? value : LowHandVal_NOTHING;
}

static enum_gameparams_t enum_gameparams[] = {
    /* must be in same order as enum_game_t */
    /* {game, minpocket, maxpocket, maxboard, haslopot, hashipot, low_qualifier, name} */
    {game_holdem, 2, 2, 5, 0, 1, LOW_QUALIFIER_NONE, "Holdem Hi"},
    {game_holdem8, 2, 2, 5, 1, 1, LOW_QUALIFIER_8, "Holdem Hi/Low 8-or-better"},
    {game_omaha, 4, 4, 5, 0, 1, LOW_QUALIFIER_NONE, "Omaha Hi"},
    {game_omaha5, 5, 5, 5, 0, 1, LOW_QUALIFIER_NONE, "Omaha Hi 5cards"},
    {game_omaha6, 6, 6, 5, 0, 1, LOW_QUALIFIER_NONE, "Omaha Hi 6cards"},
    {game_omaha8, 4, 4, 5, 1, 1, LOW_QUALIFIER_8, "Omaha Hi/Low 8-or-better"},
    {game_omaha85, 5, 5, 5, 1, 1, LOW_QUALIFIER_8, "Omaha 5cards Hi/Low 8-or-better"},
    {game_omaha86, 6, 6, 5, 1, 1, LOW_QUALIFIER_8, "Omaha 6cards Hi/Low 8-or-better"},
    {game_7stud, 3, 7, 0, 0, 1, LOW_QUALIFIER_NONE, "7-card Stud Hi"},
    {game_7stud8, 3, 7, 0, 1, 1, LOW_QUALIFIER_8, "7-card Stud Hi/Low 8-or-better"},
    {game_7studnsq, 3, 7, 0, 1, 1, LOW_QUALIFIER_NONE, "7-card Stud Hi/Low no qualifier"},
    {game_razz, 3, 7, 0, 1, 0, LOW_QUALIFIER_NONE, "Razz (7-card Stud A-5 Low)"},
    {game_5draw, 0, 5, 0, 0, 1, LOW_QUALIFIER_NONE, "5-card Draw Hi with joker"},
    {game_5draw8, 0, 5, 0, 1, 1, LOW_QUALIFIER_8, "5-card Draw Hi/Low 8-or-better with joker"},
    {game_5drawnsq, 0, 5, 0, 1, 1, LOW_QUALIFIER_NONE, "5-card Draw Hi/Low no qualifier with joker"},
    {game_lowball, 0, 5, 0, 1, 0, LOW_QUALIFIER_NONE, "5-card Draw A-5 Lowball with joker"},
    {game_lowball27, 0, 5, 0, 1, 0, LOW_QUALIFIER_NONE, "5-card Draw 2-7 Lowball"},
    {game_sdholdem, 2, 2, 5, 0, 1, LOW_QUALIFIER_NONE, "ShortDeck Holdem NL Hi"},
    {game_doubleflop_holdem, 2, 2, 10, 0, 1, LOW_QUALIFIER_NONE, "Double Flop Holdem Hi"},
    {game_drawmaha, 5, 5, 5, 0, 1, LOW_QUALIFIER_NONE, "Drawmaha (Sviten Special)"},
    {game_pineapple, 3, 3, 5, 0, 1, LOW_QUALIFIER_NONE, "Pineapple Holdem"},
    {game_pineapple8, 3, 3, 5, 1, 1, LOW_QUALIFIER_8, "Pineapple Hi/Lo"},
    {game_27_triple_draw, 5, 5, 0, 1, 0, LOW_QUALIFIER_NONE, "2-7 Triple Draw"},
    {game_a5_triple_draw, 5, 5, 0, 1, 0, LOW_QUALIFIER_NONE, "A-5 Triple Draw"},
    {game_badacey, 5, 5, 5, 1, 1, LOW_QUALIFIER_NONE, "Badacey"},
    {game_badeucy, 5, 5, 5, 1, 1, LOW_QUALIFIER_NONE, "Badeucy"},
    {game_badugi, 4, 4, 0, 1, 0, LOW_QUALIFIER_NONE, "Badugi"},
    {game_fusion, 2, 2, 5, 0, 1, LOW_QUALIFIER_NONE, "Fusion"},
    {game_courchevel, 5, 5, 5, 0, 1, LOW_QUALIFIER_NONE, "Courchevel"},
    {game_courchevel8, 5, 5, 5, 1, 1, LOW_QUALIFIER_8, "Courchevel Hi/Lo 8-or-better"},
    {game_irish, 4, 4, 5, 0, 1, LOW_QUALIFIER_NONE, "Irish Poker"},
    {game_ofc, 13, 13, 0, 0, 1, LOW_QUALIFIER_NONE, "Open Face Chinese"},
    {game_manila, 2, 2, 5, 0, 1, LOW_QUALIFIER_NONE, "Manila Poker"},
    {game_pineapple_crazy, 3, 3, 5, 0, 1, LOW_QUALIFIER_NONE, "Crazy Pineapple"},
    {game_pineapple_lazy, 3, 3, 5, 0, 1, LOW_QUALIFIER_NONE, "Lazy Pineapple (Tahoe)"},
};

/* INNER_LOOP is executed in every iteration of the combinatorial enumerator
   macros DECK_ENUMERATE_n_CARDS_D() and DECK_ENUMERATE_PERMUTATIONS_D.  It
   evaluates each player's hand based on the enumerated community cards and
   accumulates statistics on wins, ties, losses, and pot equity.

   Macro argument:
    evalwrap -- code that evaluates pockets[i], board, sharedCards, and/or
              unsharedCards[i] as a poker hand, then stores the result
                    in hival[i] and loval[i] and stores an error code in err
   Loop variable: either of
    StdDeck_CardMask sharedCards;
    StdDeck_CardMask unsharedCards[];
   Inputs:
    StdDeck_CardMask pockets[];
        StdDeck_CardMask board;
        int npockets;
   Outputs:
    enum_result_t *result;
*/

#define INNER_LOOP(evalwrap)                                                 \
  do                                                                         \
  {                                                                          \
    int i;                                                                   \
    HandVal hival[ENUM_MAXPLAYERS];                                          \
    LowHandVal loval[ENUM_MAXPLAYERS];                                       \
    HandVal besthi = HandVal_NOTHING;                                        \
    LowHandVal bestlo = LowHandVal_NOTHING;                                  \
    int hishare = 0;                                                         \
    int loshare = 0;                                                         \
    double hipot, lopot;                                                     \
    /* find winning hands for high and low */                                \
    for (i = 0; i < npockets; i++)                                           \
    {                                                                        \
      int err;                                                               \
      {                                                                      \
        evalwrap                                                             \
      }                                                                      \
      if (err != 0)                                                          \
        return 1000 + err;                                                   \
      if (hival[i] != HandVal_NOTHING)                                       \
      {                                                                      \
        if (hival[i] > besthi)                                               \
        {                                                                    \
          besthi = hival[i];                                                 \
          hishare = 1;                                                       \
        }                                                                    \
        else if (hival[i] == besthi)                                         \
        {                                                                    \
          hishare++;                                                         \
        }                                                                    \
      }                                                                      \
      if (loval[i] != LowHandVal_NOTHING)                                    \
      {                                                                      \
        if (loval[i] < bestlo)                                               \
        {                                                                    \
          bestlo = loval[i];                                                 \
          loshare = 1;                                                       \
        }                                                                    \
        else if (loval[i] == bestlo)                                         \
        {                                                                    \
          loshare++;                                                         \
        }                                                                    \
      }                                                                      \
    }                                                                        \
    /* now award pot fractions to winning hands */                           \
    if (likely(bestlo != LowHandVal_NOTHING &&                               \
               besthi != HandVal_NOTHING))                                   \
    {                                                                        \
      hipot = get_hishare_half_reciprocal(hishare);                          \
      lopot = get_hishare_half_reciprocal(loshare);                          \
    }                                                                        \
    else if (likely(bestlo == LowHandVal_NOTHING &&                          \
                    besthi != HandVal_NOTHING))                              \
    {                                                                        \
      hipot = get_hishare_reciprocal(hishare);                               \
      lopot = 0;                                                             \
    }                                                                        \
    else if (unlikely(bestlo != LowHandVal_NOTHING &&                        \
                      besthi == HandVal_NOTHING))                            \
    {                                                                        \
      hipot = 0;                                                             \
      lopot = get_hishare_reciprocal(loshare);                               \
    }                                                                        \
    else                                                                     \
    {                                                                        \
      hipot = lopot = 0;                                                     \
    }                                                                        \
    for (i = 0; i < npockets; i++)                                           \
    {                                                                        \
      double potfrac = 0;                                                    \
      int H = 0, L = 0;                                                      \
      if (hival[i] != HandVal_NOTHING)                                       \
      {                                                                      \
        if (hival[i] == besthi)                                              \
        {                                                                    \
          H = hishare;                                                       \
          potfrac += hipot;                                                  \
          if (hishare == 1)                                                  \
            result->nwinhi[i]++;                                             \
          else                                                               \
            result->ntiehi[i]++;                                             \
        }                                                                    \
        else                                                                 \
        {                                                                    \
          result->nlosehi[i]++;                                              \
        }                                                                    \
      }                                                                      \
      if (loval[i] != LowHandVal_NOTHING)                                    \
      {                                                                      \
        if (loval[i] == bestlo)                                              \
        {                                                                    \
          L = loshare;                                                       \
          potfrac += lopot;                                                  \
          if (loshare == 1)                                                  \
            result->nwinlo[i]++;                                             \
          else                                                               \
            result->ntielo[i]++;                                             \
        }                                                                    \
        else                                                                 \
        {                                                                    \
          result->nloselo[i]++;                                              \
        }                                                                    \
      }                                                                      \
      result->nsharehi[i][H]++;                                              \
      result->nsharelo[i][L]++;                                              \
      result->nshare[i][H][L]++;                                             \
      if (besthi != HandVal_NOTHING && hival[i] == besthi && hishare == 1 &&       \
          (bestlo == LowHandVal_NOTHING || (loval[i] == bestlo && loshare == 1)))  \
      {                                                                      \
        result->nscoop[i]++;                                                 \
      }                                                                      \
      result->ev[i] += potfrac;                                              \
    }                                                                        \
    if (result->ordering != NULL)                                            \
    {                                                                        \
      if (result->ordering->mode == enum_ordering_mode_hi)                   \
      {                                                                      \
        int hiranks[ENUM_ORDERING_MAXPLAYERS];                               \
        ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);    \
        ENUM_ORDERING_INCREMENT(result->ordering, npockets, hiranks);        \
      }                                                                      \
      if (result->ordering->mode == enum_ordering_mode_lo)                   \
      {                                                                      \
        int loranks[ENUM_ORDERING_MAXPLAYERS];                               \
        ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks); \
        ENUM_ORDERING_INCREMENT(result->ordering, npockets, loranks);        \
      }                                                                      \
      if (result->ordering->mode == enum_ordering_mode_hilo)                 \
      {                                                                      \
        int hiranks[ENUM_ORDERING_MAXPLAYERS_HILO];                          \
        int loranks[ENUM_ORDERING_MAXPLAYERS_HILO];                          \
        ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);    \
        ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks); \
        ENUM_ORDERING_INCREMENT_HILO(result->ordering, npockets,             \
                                     hiranks, loranks);                      \
      }                                                                      \
    }                                                                        \
    result->nsamples++;                                                      \
  } while (0);

#define INNER_LOOP_HOLDEM                                 \
  INNER_LOOP({                                            \
    StdDeck_CardMask _hand;                               \
    StdDeck_CardMask _finalBoard;                         \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards); \
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);  \
    hival[i] = StdDeck_StdRules_EVAL_N_Cached(_hand, 7);  \
    loval[i] = LowHandVal_NOTHING;                        \
    err = 0;                                              \
  })

#define INNER_LOOP_SDHOLDEM                                 \
  INNER_LOOP({                                              \
    ShortDeck_CardMask _hand;                               \
    ShortDeck_CardMask _finalBoard;                         \
    ShortDeck_CardMask_OR(_finalBoard, board, sharedCards); \
    ShortDeck_CardMask_OR(_hand, pockets[i], _finalBoard);  \
    hival[i] = ShortDeck_ShortRules_EVAL_N(_hand, 7);       \
    loval[i] = LowHandVal_NOTHING;                          \
    err = 0;                                                \
  })

#define INNER_LOOP_HOLDEM8                                \
  INNER_LOOP({                                            \
    StdDeck_CardMask _hand;                               \
    StdDeck_CardMask _finalBoard;                         \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards); \
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);  \
    hival[i] = StdDeck_StdRules_EVAL_N_Cached(_hand, 7);  \
    loval[i] = StdDeck_Lowball8_EVAL(_hand, 7);           \
    loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
    err = 0;                                              \
  })

static inline int evaluate_best_two_hole_holdem(StdDeck_CardMask pocket,
                                                StdDeck_CardMask final_board,
                                                int expected_hole_cards,
                                                HandVal *value)
{
  int hole_cards[4];
  int hole_count = 0;
  HandVal best = HandVal_NOTHING;

  if (!value || expected_hole_cards < 2 || expected_hole_cards > 4 ||
      StdDeck_numCards(final_board) != 5)
    return 1;

  for (int card = 0; card < StdDeck_N_CARDS; ++card)
  {
    if (StdDeck_CardMask_CARD_IS_SET(pocket, card))
    {
      if (hole_count >= expected_hole_cards)
        return 1;
      hole_cards[hole_count++] = card;
    }
  }
  if (hole_count != expected_hole_cards)
    return 1;

  for (int first = 0; first < hole_count - 1; ++first)
  {
    for (int second = first + 1; second < hole_count; ++second)
    {
      StdDeck_CardMask candidate;
      StdDeck_CardMask_RESET(candidate);
      StdDeck_CardMask_SET(candidate, hole_cards[first]);
      StdDeck_CardMask_SET(candidate, hole_cards[second]);
      StdDeck_CardMask_OR(candidate, candidate, final_board);
      HandVal current = StdDeck_StdRules_EVAL_N_Cached(candidate, 7);
      if (current > best)
        best = current;
    }
  }

  *value = best;
  return best == HandVal_NOTHING;
}

/* Hi/Lo variant of the above: choose the best two hole cards independently
 * for the high and the low hand (like Omaha Hi/Lo, the low may use a
 * different pair than the high). The low result is already qualified. */
static int evaluate_best_two_hole_holdem8(StdDeck_CardMask pocket,
                                          StdDeck_CardMask final_board,
                                          int expected_hole_cards,
                                          HandVal *hivalue,
                                          HandVal *lovalue)
{
  int hole_cards[4];
  int hole_count = 0;
  HandVal best_hi = HandVal_NOTHING;
  LowHandVal best_lo = LowHandVal_NOTHING;

  if (!hivalue || !lovalue || expected_hole_cards < 2 || expected_hole_cards > 4 ||
      StdDeck_numCards(final_board) != 5)
    return 1;

  for (int card = 0; card < StdDeck_N_CARDS; ++card)
  {
    if (StdDeck_CardMask_CARD_IS_SET(pocket, card))
    {
      if (hole_count >= expected_hole_cards)
        return 1;
      hole_cards[hole_count++] = card;
    }
  }
  if (hole_count != expected_hole_cards)
    return 1;

  for (int first = 0; first < hole_count - 1; ++first)
  {
    for (int second = first + 1; second < hole_count; ++second)
    {
      StdDeck_CardMask candidate;
      StdDeck_CardMask_RESET(candidate);
      StdDeck_CardMask_SET(candidate, hole_cards[first]);
      StdDeck_CardMask_SET(candidate, hole_cards[second]);
      StdDeck_CardMask_OR(candidate, candidate, final_board);
      HandVal current = StdDeck_StdRules_EVAL_N_Cached(candidate, 7);
      if (current > best_hi)
        best_hi = current;
      LowHandVal low = StdDeck_Lowball8_EVAL(candidate, 7);
      low = apply_low_qualifier(low, LOW_QUALIFIER_8);
      if (low < best_lo)
        best_lo = low;
    }
  }

  *hivalue = best_hi;
  *lovalue = best_lo;
  return best_hi == HandVal_NOTHING;
}

/* Commit each player's Crazy Pineapple discard.
 *
 * In Crazy Pineapple the third card is thrown away after the flop, so the
 * decision may use the flop but not the turn or the river. Modelling it as
 * "best two of three against the final board" - what game_pineapple does -
 * would hand the player information they did not have and overstate equity.
 *
 * For every player and every candidate discard, this measures that player's
 * equity over all runouts that were still possible when the decision was made,
 * with opponents holding all three cards, then keeps the best pair. The chosen
 * two cards are what the caller plays out.
 *
 * `board` must contain at least a flop; only its first three cards inform the
 * decision. Returns 0 on success, 1 if the flop is unknown or a pocket is
 * malformed.
 */
int pe_crazy_pineapple_commit(StdDeck_CardMask pockets[], int npockets,
                              StdDeck_CardMask board, StdDeck_CardMask dead,
                              StdDeck_CardMask committed[])
{
  StdDeck_CardMask flop;
  StdDeck_CardMask decided_dead;
  int flop_cards[3];
  int nflop = 0;

  if (npockets < 1 || npockets > ENUM_MAXPLAYERS)
    return 1;

  /* Only the flop is visible at decision time. */
  StdDeck_CardMask_RESET(flop);
  for (int card = 0; card < StdDeck_N_CARDS && nflop < 3; ++card)
    if (StdDeck_CardMask_CARD_IS_SET(board, card))
      flop_cards[nflop++] = card;
  if (nflop != 3)
    return 1;
  for (int i = 0; i < 3; ++i)
    StdDeck_CardMask_SET(flop, flop_cards[i]);

  /* Cards that were already accounted for when the discard was made: every
     player's three hole cards, the flop, and any dead cards. The turn and
     river are unknown at that point even when the caller supplied them. */
  StdDeck_CardMask_RESET(decided_dead);
  StdDeck_CardMask_OR(decided_dead, decided_dead, flop);
  StdDeck_CardMask_OR(decided_dead, decided_dead, dead);
  for (int p = 0; p < npockets; ++p)
    StdDeck_CardMask_OR(decided_dead, decided_dead, pockets[p]);

  for (int p = 0; p < npockets; ++p)
  {
    int hole[3];
    int nhole = 0;
    double best_equity = -1.0;

    for (int card = 0; card < StdDeck_N_CARDS && nhole < 3; ++card)
      if (StdDeck_CardMask_CARD_IS_SET(pockets[p], card))
        hole[nhole++] = card;
    if (nhole != 3)
      return 1;

    StdDeck_CardMask_RESET(committed[p]);

    for (int discard = 0; discard < 3; ++discard)
    {
      StdDeck_CardMask keep;
      double equity = 0.0;
      long runouts = 0;

      StdDeck_CardMask_RESET(keep);
      for (int i = 0; i < 3; ++i)
        if (i != discard)
          StdDeck_CardMask_SET(keep, hole[i]);

      /* Enumerate every turn/river pair that was still live at the flop. */
      for (int turn = 0; turn < StdDeck_N_CARDS; ++turn)
      {
        if (StdDeck_CardMask_CARD_IS_SET(decided_dead, turn))
          continue;
        for (int river = turn + 1; river < StdDeck_N_CARDS; ++river)
        {
          StdDeck_CardMask final_board;
          HandVal mine;
          StdDeck_CardMask mine_hand;
          int winners = 1;
          int beaten = 0;

          if (StdDeck_CardMask_CARD_IS_SET(decided_dead, river))
            continue;

          final_board = flop;
          StdDeck_CardMask_SET(final_board, turn);
          StdDeck_CardMask_SET(final_board, river);

          StdDeck_CardMask_OR(mine_hand, keep, final_board);
          mine = StdDeck_StdRules_EVAL_N(mine_hand, 7);

          /* Opponents still hold three cards and play their best two. */
          for (int o = 0; o < npockets && !beaten; ++o)
          {
            HandVal theirs;
            if (o == p)
              continue;
            if (evaluate_best_two_hole_holdem(pockets[o], final_board, 3, &theirs))
              return 1;
            if (theirs > mine)
              beaten = 1;
            else if (theirs == mine)
              winners++;
          }

          if (!beaten)
            equity += 1.0 / (double)winners;
          runouts++;
        }
      }

      if (runouts > 0)
        equity /= (double)runouts;

      if (equity > best_equity)
      {
        best_equity = equity;
        committed[p] = keep;
      }
    }

    if (best_equity < 0.0)
      return 1;
  }

  return 0;
}

#define INNER_LOOP_DISCARD_HOLDEM(expected_hole_cards)       \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    err = evaluate_best_two_hole_holdem(pockets[i],           \
                                        _finalBoard,          \
                                        (expected_hole_cards),\
                                        &hival[i]);           \
    loval[i] = LowHandVal_NOTHING;                            \
  })

#define INNER_LOOP_PINEAPPLE INNER_LOOP_DISCARD_HOLDEM(3)
#define INNER_LOOP_IRISH INNER_LOOP_DISCARD_HOLDEM(4)

/* Lazy Pineapple (Tahoe): all three hole cards reach showdown and the best two
   play. That is what INNER_LOOP_DISCARD_HOLDEM(3) already computes, so this is
   deliberately the same evaluation as game_pineapple; the separate game exists
   so callers can name the variant they mean. */
#define INNER_LOOP_PINEAPPLE_LAZY INNER_LOOP_DISCARD_HOLDEM(3)

/* Crazy Pineapple: the discard is committed on the flop, so the two surviving
   cards are fixed before this loop runs (see pe_crazy_pineapple_commit) and play
   out as an ordinary Hold'em pocket. */
#define INNER_LOOP_PINEAPPLE_CRAZY                            \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask _crazyHand;                              \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    StdDeck_CardMask_OR(_crazyHand, _committed[i],            \
                        _finalBoard);                         \
    hival[i] = StdDeck_StdRules_EVAL_N(_crazyHand, 7);        \
    loval[i] = LowHandVal_NOTHING;                            \
    err = 0;                                                  \
  })

#define INNER_LOOP_PINEAPPLE8                                 \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    err = evaluate_best_two_hole_holdem8(pockets[i],          \
                                         _finalBoard,         \
                                         3,                   \
                                         &hival[i],           \
                                         &loval[i]);          \
  })

#define INNER_LOOP_OMAHA                                      \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], NULL);        \
    }                                                         \
    loval[i] = LowHandVal_NOTHING;                            \
  })

#define INNER_LOOP_OMAHA5                                     \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], NULL);        \
    }                                                         \
    loval[i] = LowHandVal_NOTHING;                            \
  })

#define INNER_LOOP_OMAHA6                                     \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], NULL);        \
    }                                                         \
    loval[i] = LowHandVal_NOTHING;                            \
  })

#define INNER_LOOP_OMAHA8                                     \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      loval[i] = LowHandVal_NOTHING;                          \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], &loval[i]);   \
      if (err == 0)                                           \
        loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
      else                                                    \
        loval[i] = LowHandVal_NOTHING;                        \
    }                                                         \
  })

#define INNER_LOOP_OMAHA85                                    \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      loval[i] = LowHandVal_NOTHING;                          \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], &loval[i]);   \
      if (err == 0)                                           \
        loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
      else                                                    \
        loval[i] = LowHandVal_NOTHING;                        \
    }                                                         \
  })

#define INNER_LOOP_OMAHA86                                    \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Omaha evaluation */       \
      hival[i] = HandVal_NOTHING;                             \
      loval[i] = LowHandVal_NOTHING;                          \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], &loval[i]);   \
      if (err == 0)                                           \
        loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
      else                                                    \
        loval[i] = LowHandVal_NOTHING;                        \
    }                                                         \
  })

/* Courchevel: 5 hole cards, Omaha-style evaluation (2+3 rule) */
#define INNER_LOOP_COURCHEVEL                                 \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Courchevel evaluation */  \
      hival[i] = HandVal_NOTHING;                             \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], NULL);        \
    }                                                         \
    loval[i] = LowHandVal_NOTHING;                            \
  })

/* Courchevel Hi/Lo: 5 hole cards, Omaha Hi/Lo-style evaluation */
#define INNER_LOOP_COURCHEVEL8                                \
  INNER_LOOP({                                                \
    StdDeck_CardMask _finalBoard;                             \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);     \
    int nboard = StdDeck_numCards(_finalBoard);               \
    if (nboard < 3)                                           \
    {                                                         \
      /* Not enough board cards for Courchevel evaluation */  \
      hival[i] = HandVal_NOTHING;                             \
      loval[i] = LowHandVal_NOTHING;                          \
      err = 0;                                                \
    }                                                         \
    else                                                      \
    {                                                         \
      err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard, \
                                     &hival[i], &loval[i]);   \
      if (err == 0)                                           \
        loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
      else                                                    \
        loval[i] = LowHandVal_NOTHING;                        \
    }                                                         \
  })

#define INNER_LOOP_DRAWMAHA                                             \
  INNER_LOOP({                                                          \
    StdDeck_CardMask _finalBoard;                                       \
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);               \
    int nboard = StdDeck_numCards(_finalBoard);                         \
    if (nboard < 3)                                                     \
    {                                                                   \
      /* Not enough board cards for Drawmaha evaluation */              \
      hival[i] = HandVal_NOTHING;                                       \
      loval[i] = LowHandVal_NOTHING;                                    \
      err = 0;                                                          \
    }                                                                   \
    else                                                                \
    {                                                                   \
      err = StdDeck_DrawmahaRules_EVAL_OPTIMAL(pockets[i], _finalBoard, \
                                               &hival[i], &loval[i]);   \
    }                                                                   \
  })

#define INNER_LOOP_7STUD                                      \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);             \
    loval[i] = LowHandVal_NOTHING;                            \
    err = 0;                                                  \
  })

#define INNER_LOOP_7STUD8                                     \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);             \
    loval[i] = StdDeck_Lowball8_EVAL(_hand, 7);               \
    loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
    err = 0;                                                  \
  })

#define INNER_LOOP_7STUDNSQ                                   \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);             \
    loval[i] = pe_eval_low_a5(_hand);                         \
    err = 0;                                                  \
  })

#define INNER_LOOP_RAZZ                                       \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = HandVal_NOTHING;                               \
    loval[i] = pe_eval_low_a5(_hand);                         \
    err = 0;                                                  \
  })

#define INNER_LOOP_BADUGI                                     \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = (HandVal)StdDeck_BadugiRules_EVAL_N(            \
        _hand, StdDeck_numCards(_hand));                      \
    loval[i] = LowHandVal_NOTHING;                            \
    err = 0;                                                  \
  })

#define INNER_LOOP_5DRAW                                      \
  INNER_LOOP({                                                \
    JokerDeck_CardMask _hand;                                 \
    JokerDeck_CardMask _jpocket;                              \
    Universal_ConvertStdToJoker(pockets[i], &_jpocket);       \
    JokerDeck_CardMask_OR(_hand, _jpocket, unsharedCards[i]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);         \
    loval[i] = LowHandVal_NOTHING;                            \
    err = 0;                                                  \
  })

#define INNER_LOOP_5DRAW8                                     \
  INNER_LOOP({                                                \
    JokerDeck_CardMask _hand;                                 \
    JokerDeck_CardMask _jpocket;                              \
    Universal_ConvertStdToJoker(pockets[i], &_jpocket);       \
    JokerDeck_CardMask_OR(_hand, _jpocket, unsharedCards[i]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);         \
    loval[i] = JokerDeck_Lowball8_EVAL(_hand, 5);             \
    loval[i] = apply_low_qualifier(loval[i], LOW_QUALIFIER_8); \
    err = 0;                                                  \
  })

#define INNER_LOOP_5DRAWNSQ                                   \
  INNER_LOOP({                                                \
    JokerDeck_CardMask _hand;                                 \
    JokerDeck_CardMask _jpocket;                              \
    Universal_ConvertStdToJoker(pockets[i], &_jpocket);       \
    JokerDeck_CardMask_OR(_hand, _jpocket, unsharedCards[i]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);         \
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);              \
    err = 0;                                                  \
  })

#define INNER_LOOP_LOWBALL                                    \
  INNER_LOOP({                                                \
    JokerDeck_CardMask _hand;                                 \
    JokerDeck_CardMask _jpocket;                              \
    Universal_ConvertStdToJoker(pockets[i], &_jpocket);       \
    JokerDeck_CardMask_OR(_hand, _jpocket, unsharedCards[i]); \
    hival[i] = HandVal_NOTHING;                               \
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);              \
    err = 0;                                                  \
  })

#define INNER_LOOP_LOWBALL27                                  \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = HandVal_NOTHING;                               \
    loval[i] = StdDeck_Lowball27_EVAL_N(_hand, 5);            \
    err = 0;                                                  \
  })

#define INNER_LOOP_27_TRIPLE_DRAW                             \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = HandVal_NOTHING;                               \
    loval[i] = StdDeck_Lowball27_EVAL_N(_hand, 5);            \
    err = 0;                                                  \
  })

#define INNER_LOOP_A5_TRIPLE_DRAW                             \
  INNER_LOOP({                                                \
    StdDeck_CardMask _hand;                                   \
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]); \
    hival[i] = HandVal_NOTHING;                               \
    loval[i] = pe_eval_low_a5(_hand);                         \
    err = 0;                                                  \
  })

#define INNER_LOOP_DOUBLEFLOP_HOLDEM(evalwrap1, evalwrap2)                             \
  do                                                                                   \
  {                                                                                    \
    int i;                                                                             \
    HandVal hival1[ENUM_MAXPLAYERS], hival2[ENUM_MAXPLAYERS];                          \
    HandVal besthi1 = HandVal_NOTHING, besthi2 = HandVal_NOTHING;                      \
    int hishare1 = 0, hishare2 = 0;                                                    \
    double hipot1, hipot2;                                                             \
                                                                                       \
    /* First pass: evaluate all hands and find best hands */                           \
    for (i = 0; i < npockets; i++)                                                     \
    {                                                                                  \
      /* Evaluate hand for first board */                                              \
      {                                                                                \
        evalwrap1                                                                      \
      }                                                                                \
      /* Evaluate hand for second board */                                             \
      {                                                                                \
        evalwrap2                                                                      \
      }                                                                                \
    }                                                                                  \
                                                                                       \
    /* Find best hands for both boards */                                              \
    for (i = 0; i < npockets; i++)                                                     \
    {                                                                                  \
      if (hival1[i] > besthi1)                                                         \
      {                                                                                \
        besthi1 = hival1[i];                                                           \
        hishare1 = 1;                                                                  \
      }                                                                                \
      else if (hival1[i] == besthi1)                                                   \
      {                                                                                \
        hishare1++;                                                                    \
      }                                                                                \
      if (hival2[i] > besthi2)                                                         \
      {                                                                                \
        besthi2 = hival2[i];                                                           \
        hishare2 = 1;                                                                  \
      }                                                                                \
      else if (hival2[i] == besthi2)                                                   \
      {                                                                                \
        hishare2++;                                                                    \
      }                                                                                \
    }                                                                                  \
                                                                                       \
    /* Pot distribution for each board */                                              \
    hipot1 = besthi1 != HandVal_NOTHING ? get_hishare_reciprocal(hishare1) : 0;        \
    hipot2 = besthi2 != HandVal_NOTHING ? get_hishare_reciprocal(hishare2) : 0;        \
                                                                                       \
    /* Award pots and update statistics */                                             \
    for (i = 0; i < npockets; i++)                                                     \
    {                                                                                  \
      double potfrac = 0;                                                              \
      int wins = 0, ties = 0, losses = 0;                                              \
                                                                                       \
      /* Board 1 results */                                                            \
      if (hival1[i] == besthi1)                                                        \
      {                                                                                \
        potfrac += hipot1 / 2; /* Half pot from board 1 */                             \
        if (hishare1 == 1)                                                             \
          wins++;                                                                      \
        else                                                                           \
          ties++;                                                                      \
      }                                                                                \
      else                                                                             \
      {                                                                                \
        losses++;                                                                      \
      }                                                                                \
                                                                                       \
      /* Board 2 results */                                                            \
      if (hival2[i] == besthi2)                                                        \
      {                                                                                \
        potfrac += hipot2 / 2; /* Half pot from board 2 */                             \
        if (hishare2 == 1)                                                             \
          wins++;                                                                      \
        else                                                                           \
          ties++;                                                                      \
      }                                                                                \
      else                                                                             \
      {                                                                                \
        losses++;                                                                      \
      }                                                                                \
                                                                                       \
      /* Update EV */                                                                  \
      result->ev[i] += potfrac;                                                        \
                                                                                       \
      /* Update win/tie/loss counters based on overall result */                       \
      if (wins == 2)                                                                   \
      {                                                                                \
        /* Won both boards outright */                                                 \
        result->nwinhi[i]++;                                                           \
        result->nscoop[i]++;                                                           \
      }                                                                                \
      else if (wins == 1 && ties == 1)                                                 \
      {                                                                                \
        /* Won one, tied one */                                                        \
        result->nwinhi[i]++;                                                           \
      }                                                                                \
      else if (wins == 1 && losses == 1)                                               \
      {                                                                                \
        /* Won one, lost one - this is a tie overall */                                \
        result->ntiehi[i]++;                                                           \
      }                                                                                \
      else if (ties == 2)                                                              \
      {                                                                                \
        /* Tied both boards */                                                         \
        result->ntiehi[i]++;                                                           \
      }                                                                                \
      else if (ties == 1 && losses == 1)                                               \
      {                                                                                \
        /* Tied one, lost one */                                                       \
        result->nlosehi[i]++;                                                          \
      }                                                                                \
      else if (losses == 2)                                                            \
      {                                                                                \
        /* Lost both boards */                                                         \
        result->nlosehi[i]++;                                                          \
      }                                                                                \
    }                                                                                  \
                                                                                       \
    /* Update ordering histogram for hihi mode if applicable */                        \
    if (result->ordering != NULL && result->ordering->mode == enum_ordering_mode_hihi) \
    {                                                                                  \
      int hiranks1[ENUM_ORDERING_MAXPLAYERS], hiranks2[ENUM_ORDERING_MAXPLAYERS];      \
      ENUM_ORDERING_RANK_HI(hival1, HandVal_NOTHING, npockets, hiranks1);              \
      ENUM_ORDERING_RANK_HI(hival2, HandVal_NOTHING, npockets, hiranks2);              \
      for (i = 0; i < npockets; i++)                                                   \
      {                                                                                \
        int rank1 = hiranks1[i], rank2 = hiranks2[i];                                  \
        int combinedRank = rank1 * ENUM_ORDERING_MAXPLAYERS + rank2;                   \
        if (combinedRank < result->ordering->nentries)                                 \
        {                                                                              \
          result->ordering->hist[combinedRank]++;                                      \
        }                                                                              \
      }                                                                                \
    }                                                                                  \
    result->nsamples++;                                                                \
  } while (0)

int enumExhaustive(enum_game_t game, StdDeck_CardMask pockets[],
                   StdDeck_CardMask board, StdDeck_CardMask dead,
                   int npockets, int nboard, int orderflag,
                   enum_result_t *result)
{
  int idx;

  enumResultClear(result);
  if (npockets > ENUM_MAXPLAYERS)
    return 1;
  /* Optional: initialize global eval cache for exhaustive if requested */
  {
    static int _cache_checked = 0;
    if (!_cache_checked)
    {
      _cache_checked = 1;
      const char *env_mb = getenv("PE_EXHAUSTIVE_CACHE_MB");
      if (env_mb && *env_mb)
      {
        long mb = strtol(env_mb, NULL, 10);
        if (mb > 0)
        {
          size_t entries = (size_t)mb * 1024ULL * 1024ULL / sizeof(eval_cache_entry_t);
          if (entries < EVAL_CACHE_MIN_SIZE)
            entries = EVAL_CACHE_MIN_SIZE;
          if (entries > EVAL_CACHE_MAX_SIZE)
            entries = EVAL_CACHE_MAX_SIZE;
          eval_cache_init_global((uint32_t)entries);
        }
      }
    }
  }
  /* Ensure we never enumerate cards already in board or pockets */
  StdDeck_CardMask effective_dead = dead;
  for (idx = 0; idx < npockets; idx++)
  {
    StdDeck_CardMask_OR(effective_dead, effective_dead, pockets[idx]);
  }
  StdDeck_CardMask_OR(effective_dead, effective_dead, board);
  if (orderflag)
  {
    enum_ordering_mode_t mode;
    switch (game)
    {
    case game_holdem:
    case game_omaha:
    case game_omaha5:
    case game_omaha6:
    case game_7stud:
    case game_5draw:
    case game_sdholdem:
      mode = enum_ordering_mode_hi;
      break;
    case game_razz:
    case game_lowball:
    case game_lowball27:
      mode = enum_ordering_mode_lo;
      break;
    case game_holdem8:
    case game_omaha8:
    case game_omaha85:
    case game_omaha86:
    case game_7stud8:
    case game_7studnsq:
    case game_5draw8:
    case game_5drawnsq:
    case game_pineapple8:
      mode = enum_ordering_mode_hilo;
      break;
    case game_doubleflop_holdem:
      mode = enum_ordering_mode_hihi;
      break;
    case game_drawmaha:
    case game_pineapple:
    case game_pineapple_crazy:
    case game_pineapple_lazy:
    case game_27_triple_draw:
    case game_a5_triple_draw:
    case game_badacey:
    case game_badeucy:
    case game_badugi:
    case game_fusion:
    case game_courchevel:
    case game_courchevel8:
    case game_irish:
    case game_ofc:
    case game_manila:
      mode = enum_ordering_mode_hi;
      break;
    case game_NUMGAMES:
    default:
      return 1;
    }
    if (enumResultAlloc(result, npockets, mode))
      return 1;
  }

  if (game == game_holdem)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_sdholdem)
  {
    ShortDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_SHORTDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_SDHOLDEM);
    }
    else if (nboard == 3)
    {
      ENUM_SHORTDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_SDHOLDEM);
    }
    else if (nboard == 4)
    {
      ENUM_SHORTDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_SDHOLDEM);
    }
    else if (nboard == 5)
    {
      ShortDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_SDHOLDEM;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_doubleflop_holdem)
  {
    StdDeck_CardMask sharedCards1, sharedCards2;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_KxK_FROM_DEAD(5, 5, effective_dead, sharedCards1, sharedCards2,
                                           INNER_LOOP_DOUBLEFLOP_HOLDEM({
          StdDeck_CardMask _finalBoard1;
          StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
          StdDeck_CardMask _hand1;
          StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
          hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7); }, {
          StdDeck_CardMask _finalBoard2;
          StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
          StdDeck_CardMask _hand2;
          StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
          hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7); }));
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_KxK_FROM_DEAD(2, 2, effective_dead, sharedCards1, sharedCards2,
                                           INNER_LOOP_DOUBLEFLOP_HOLDEM({
          StdDeck_CardMask _finalBoard1;
          StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
          StdDeck_CardMask _hand1;
          StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
          hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7); }, {
          StdDeck_CardMask _finalBoard2;
          StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
          StdDeck_CardMask _hand2;
          StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
          hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7); }));
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_KxK_FROM_DEAD(1, 1, effective_dead, sharedCards1, sharedCards2,
                                           INNER_LOOP_DOUBLEFLOP_HOLDEM({
          StdDeck_CardMask _finalBoard1;
          StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
          StdDeck_CardMask _hand1;
          StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
          hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7); }, {
          StdDeck_CardMask _finalBoard2;
          StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
          StdDeck_CardMask _hand2;
          StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
          hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7); }));
    }
    else if (nboard == 5)
    {
      /* For double flop with 5 board cards, we need to handle this differently */
      /* This case is not yet implemented */
      return 1;
      /*
      StdDeck_CardMask board1, board2;
      INNER_LOOP_DOUBLEFLOP_HOLDEM({
              StdDeck_CardMask _finalBoard1 = board1;
              StdDeck_CardMask _hand1;
              StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
              hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7); }, {
              StdDeck_CardMask _finalBoard2 = board2;
              StdDeck_CardMask _hand2;
              StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
              hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7); });
      */
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_holdem8)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_HOLDEM8);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_HOLDEM8);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_HOLDEM8);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM8;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha5)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA5);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA5);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA5);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA5;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha6)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA6);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA6);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA6);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA6;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha8)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA8);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA8);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA8);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA8;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha85)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA85);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA85);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA85);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA85;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_omaha86)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_OMAHA86);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_OMAHA86);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_OMAHA86);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA86;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_courchevel)
  {
    /* Courchevel: 5 hole cards, one flop card revealed pre-flop */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      /* Pre-flop without revealed card: enumerate all 5 community cards */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL);
    }
    else if (nboard == 1)
    {
      /* Pre-flop with one revealed card: enumerate remaining 4 community cards */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(4, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL);
    }
    else if (nboard == 3)
    {
      /* Flop: enumerate turn and river */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL);
    }
    else if (nboard == 4)
    {
      /* Turn: enumerate river */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL);
    }
    else if (nboard == 5)
    {
      /* River: final evaluation */
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_COURCHEVEL;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_courchevel8)
  {
    /* Courchevel Hi/Lo: 5 hole cards, one flop card revealed pre-flop */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      /* Pre-flop without revealed card: enumerate all 5 community cards */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL8);
    }
    else if (nboard == 1)
    {
      /* Pre-flop with one revealed card: enumerate remaining 4 community cards */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(4, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL8);
    }
    else if (nboard == 3)
    {
      /* Flop: enumerate turn and river */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL8);
    }
    else if (nboard == 4)
    {
      /* Turn: enumerate river */
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_COURCHEVEL8);
    }
    else if (nboard == 5)
    {
      /* River: final evaluation */
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_COURCHEVEL8;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_pineapple)
  {
    /* Choose the optimal discard, then evaluate the remaining two cards as
       a normal Hold'em pocket against the complete board. */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_PINEAPPLE;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_pineapple8)
  {
    /* Pineapple Hi/Lo: choose the best two hole cards independently for the
       high and the low hand, evaluated against the complete board. */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE8);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE8);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE8);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_PINEAPPLE8;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_pineapple_lazy)
  {
    /* All three cards reach showdown and the best two play: the same
       evaluation as game_pineapple, exposed under its own name. */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE_LAZY);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE_LAZY);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE_LAZY);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_PINEAPPLE_LAZY;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_pineapple_crazy)
  {
    /* The discard is committed on the flop, so it is resolved once here and the
       surviving two cards then play out as an ordinary Hold'em pocket. Without
       a flop there is nothing to decide on, so those states are rejected rather
       than silently evaluated with turn/river knowledge. */
    StdDeck_CardMask sharedCards;
    StdDeck_CardMask _committed[ENUM_MAXPLAYERS];

    if (nboard < 3 || nboard > 5)
      return 1;
    if (pe_crazy_pineapple_commit(pockets, npockets, board, dead, _committed))
      return 1;

    if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE_CRAZY);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_PINEAPPLE_CRAZY);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_PINEAPPLE_CRAZY;
    }
  }
  else if (game == game_irish)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_IRISH);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_IRISH);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_IRISH);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_IRISH;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_fusion)
  {
    /* Fusion (2 hole cards + 5 community) evaluates like Hold'em. */
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_HOLDEM);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_7stud)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_7STUD);
  }
  else if (game == game_7stud8)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_7STUD8);
  }
  else if (game == game_7studnsq)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_7STUDNSQ);
  }
  else if (game == game_razz)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_RAZZ);
  }
  else if (game == game_badugi)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 4 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, effective_dead,
                                            unsharedCards, INNER_LOOP_BADUGI);
  }
  else if (game == game_5draw)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5draw;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5draw);

    ENUM_JOKERDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, jdead_5draw, unsharedCards, INNER_LOOP_5DRAW);
  }
  else if (game == game_5draw8)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5draw8;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5draw8);

    ENUM_JOKERDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, jdead_5draw8, unsharedCards, INNER_LOOP_5DRAW8);
  }
  else if (game == game_5drawnsq)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5drawnsq;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5drawnsq);

    ENUM_JOKERDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, jdead_5drawnsq, unsharedCards, INNER_LOOP_5DRAWNSQ);
  }
  else if (game == game_lowball)
  {
    fprintf(stderr, "DEBUG: Entering game_lowball enumeration\n");
    fprintf(stderr, "DEBUG: npockets=%d, nboard=%d\n", npockets, nboard);

    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_lowball;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
      fprintf(stderr, "DEBUG: Player %d has %d cards, needs %d more\n",
              idx, JokerDeck_numCards(jpockets[idx]), numToDeal[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_lowball);
    fprintf(stderr, "DEBUG: Dead cards count = %d\n", JokerDeck_numCards(jdead_lowball));
    fprintf(stderr, "DEBUG: Calling JOKERDECK_ENUMERATE_COMBINATIONS_D\n");

    ENUM_JOKERDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, jdead_lowball, unsharedCards, INNER_LOOP_LOWBALL);

    fprintf(stderr, "DEBUG: Finished game_lowball enumeration\n");
  }
  else if (game == game_lowball27)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_LOWBALL27);
  }
  else if (game == game_drawmaha)
  {
    StdDeck_CardMask sharedCards;
    if (nboard == 0)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(5, effective_dead, sharedCards, INNER_LOOP_DRAWMAHA);
    }
    else if (nboard == 3)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(2, effective_dead, sharedCards, INNER_LOOP_DRAWMAHA);
    }
    else if (nboard == 4)
    {
      ENUM_STDDECK_ENUMERATE_K_FROM_DEAD(1, effective_dead, sharedCards, INNER_LOOP_DRAWMAHA);
    }
    else if (nboard == 5)
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_DRAWMAHA;
    }
    else
    {
      return 1;
    }
  }
  else if (game == game_27_triple_draw)
  {
    /* Triple draw is a draw poker game with no board */
    if (nboard != 0)
    {
      return 1; /* Triple draw has no board cards */
    }
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_27_TRIPLE_DRAW);
  }
  else if (game == game_a5_triple_draw)
  {
    /* A-5 Triple draw is a draw poker game with no board */
    if (nboard != 0)
    {
      return 1; /* Triple draw has no board cards */
    }
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    ENUM_STDDECK_ENUMERATE_MULTI_FROM_DEAD(npockets, numToDeal, dead, unsharedCards, INNER_LOOP_A5_TRIPLE_DRAW);
  }
  else
  {
    return 1;
  }

  result->game = game;
  result->nplayers = npockets;
  result->sampleType = ENUM_EXHAUSTIVE;
  return 0;
}

int enumSample(enum_game_t game, StdDeck_CardMask pockets[],
               StdDeck_CardMask board, StdDeck_CardMask dead,
               int npockets, int nboard, int niter, int orderflag,
               enum_result_t *result)
{
  int idx;
  int numCards;

  enumResultClear(result);
  if (npockets > ENUM_MAXPLAYERS)
    return 1;

  /* Ensure we never enumerate cards already in board or pockets */
  StdDeck_CardMask effective_dead = dead;
  for (idx = 0; idx < npockets; idx++)
  {
    StdDeck_CardMask_OR(effective_dead, effective_dead, pockets[idx]);
  }
  StdDeck_CardMask_OR(effective_dead, effective_dead, board);
  if (orderflag)
  {
    enum_ordering_mode_t mode;
    switch (game)
    {
    case game_holdem:
    case game_omaha:
    case game_omaha5:
    case game_omaha6:
    case game_7stud:
    case game_5draw:
    case game_sdholdem:
      mode = enum_ordering_mode_hi;
      break;
    case game_razz:
    case game_lowball:
    case game_lowball27:
      mode = enum_ordering_mode_lo;
      break;
    case game_holdem8:
    case game_omaha8:
    case game_omaha85:
    case game_omaha86:
    case game_7stud8:
    case game_7studnsq:
    case game_5draw8:
    case game_5drawnsq:
    case game_pineapple8:
      mode = enum_ordering_mode_hilo;
      break;
    case game_doubleflop_holdem:
      mode = enum_ordering_mode_hihi;
      break;
    case game_drawmaha:
    case game_pineapple:
    case game_pineapple_crazy:
    case game_pineapple_lazy:
    case game_27_triple_draw:
    case game_a5_triple_draw:
    case game_badacey:
    case game_badeucy:
    case game_badugi:
    case game_fusion:
    case game_courchevel:
    case game_courchevel8:
    case game_irish:
    case game_ofc:
    case game_manila:
      mode = enum_ordering_mode_hi;
      break;
    case game_NUMGAMES:
    default:
      return 1;
    }
    if (enumResultAlloc(result, npockets, mode))
      return 1;
  }

  if (game == game_holdem)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_HOLDEM);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM;
      return 1;
    }
  }
  else if (game == game_sdholdem)
  {
    ShortDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(ShortDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_SDHOLDEM);
    }
    else
    {
      ShortDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_SDHOLDEM;
      return 1;
    }
  }
  else if (game == game_holdem8)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_HOLDEM8);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM8;
      return 1;
    }
  }
  else if (game == game_omaha)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA;
      return 1;
    }
  }
  else if (game == game_omaha5)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA5);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA5;
      return 1;
    }
  }
  else if (game == game_omaha6)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA6);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA6;
      return 1;
    }
  }
  else if (game == game_omaha8)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA8);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA8;
      return 1;
    }
  }
  else if (game == game_omaha85)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA85);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA85;
      return 1;
    }
  }
  else if (game == game_omaha86)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_OMAHA86);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA86;
      return 1;
    }
  }
  else if (game == game_courchevel)
  {
    /* Courchevel: 5 hole cards, Omaha-style evaluation */
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_COURCHEVEL);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_COURCHEVEL;
      return 1;
    }
  }
  else if (game == game_courchevel8)
  {
    /* Courchevel Hi/Lo: 5 hole cards, Omaha Hi/Lo-style evaluation */
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_COURCHEVEL8);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_COURCHEVEL8;
      return 1;
    }
  }
  else if (game == game_7stud)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUD);
  }
  else if (game == game_7stud8)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUD8);
  }
  else if (game == game_7studnsq)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUDNSQ);
  }
  else if (game == game_razz)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 7 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_RAZZ);
  }
  else if (game == game_5draw)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5draw;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5draw);

    JOKERDECK_MONTECARLO_PERMUTATIONS_D(unsharedCards,
                                        npockets, numToDeal,
                                        jdead_5draw, niter, INNER_LOOP_5DRAW);
  }
  else if (game == game_5draw8)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5draw8;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5draw8);

    JOKERDECK_MONTECARLO_PERMUTATIONS_D(unsharedCards,
                                        npockets, numToDeal,
                                        jdead_5draw8, niter, INNER_LOOP_5DRAW8);
  }
  else if (game == game_5drawnsq)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_5drawnsq;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_5drawnsq);

    JOKERDECK_MONTECARLO_PERMUTATIONS_D(unsharedCards,
                                        npockets, numToDeal,
                                        jdead_5drawnsq, niter, INNER_LOOP_5DRAWNSQ);
  }
  else if (game == game_lowball)
  {
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    JokerDeck_CardMask jdead_lowball;

    /* Convert StdDeck pockets to JokerDeck */
    for (idx = 0; idx < npockets; idx++)
    {
      Universal_ConvertStdToJoker(pockets[idx], &jpockets[idx]);
      numToDeal[idx] = 5 - JokerDeck_numCards(jpockets[idx]);
    }
    Universal_ConvertStdToJoker(effective_dead, &jdead_lowball);

    JOKERDECK_MONTECARLO_PERMUTATIONS_D(unsharedCards,
                                        npockets, numToDeal,
                                        jdead_lowball, niter, INNER_LOOP_LOWBALL);
  }
  else if (game == game_lowball27)
  {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_LOWBALL27);
  }
  else if (game == game_drawmaha)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_DRAWMAHA);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_DRAWMAHA;
      }
    }
  }
  else if (game == game_pineapple)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_PINEAPPLE);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_PINEAPPLE;
      }
    }
  }
  else if (game == game_pineapple8)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_PINEAPPLE8);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_PINEAPPLE8;
      }
    }
  }
  else if (game == game_pineapple_lazy)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      /* effective_dead, not dead: the sampler must not redeal the board or
         anyone's hole cards. Most other games here still pass `dead` and
         misdeal because of it. */
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_PINEAPPLE_LAZY);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_PINEAPPLE_LAZY;
      }
    }
  }
  else if (game == game_pineapple_crazy)
  {
    /* See the exhaustive path: the discard needs a flop and is resolved once,
       before sampling the remaining board cards. */
    StdDeck_CardMask sharedCards;
    StdDeck_CardMask _committed[ENUM_MAXPLAYERS];

    if (nboard < 3 || nboard > 5)
      return 1;
    if (pe_crazy_pineapple_commit(pockets, npockets, board, dead, _committed))
      return 1;

    numCards = 5 - nboard;
    if (numCards > 0)
    {
      /* effective_dead, not dead: the sampler must not redeal the board or
         anyone's hole cards. Most other games here still pass `dead` and
         misdeal because of it. */
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_PINEAPPLE_CRAZY);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_PINEAPPLE_CRAZY;
      }
    }
  }
  else if (game == game_27_triple_draw)
  {
    if (nboard != 0)
    {
      return 1; /* Triple draw has no board cards */
    }
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_27_TRIPLE_DRAW);
  }
  else if (game == game_a5_triple_draw)
  {
    if (nboard != 0)
    {
      return 1; /* Triple draw has no board cards */
    }
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 5 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_A5_TRIPLE_DRAW);
  }
  else if (game == game_badugi)
  {
    if (nboard != 0)
    {
      return 1; /* Badugi has no board cards */
    }
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (idx = 0; idx < npockets; idx++)
      numToDeal[idx] = 4 - StdDeck_numCards(pockets[idx]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   effective_dead, niter, INNER_LOOP_BADUGI);
  }
  else if (game == game_irish)
  {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_IRISH);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_IRISH;
      }
    }
  }
  else if (game == game_fusion)
  {
    /* Fusion (2 hole cards + 5 community) evaluates like Hold'em. */
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0)
    {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, effective_dead, numCards,
                                niter, INNER_LOOP_HOLDEM);
    }
    else
    {
      StdDeck_CardMask_RESET(sharedCards);
      for (int iter = 0; iter < niter; iter++)
      {
        INNER_LOOP_HOLDEM;
      }
    }
  }
  else if (game == game_doubleflop_holdem)
  {
    fprintf(stderr, "DEBUG: Entering double flop Monte Carlo, niter=%d\n", niter);

    /* We need to sample cards for two boards */
    /* Each board needs 5 cards total */
    int cardsNeeded = 5 - nboard;           /* Cards needed per board */
    int totalCardsNeeded = cardsNeeded * 2; /* Total for both boards */

    for (int iter = 0; iter < niter; iter++)
    {
      StdDeck_CardMask availableCards;
      StdDeck_CardMask board1, board2;
      StdDeck_CardMask drawn;

      /* Start with all cards except dead ones */
      StdDeck_CardMask_NOT(availableCards, dead);
      StdDeck_CardMask_RESET(drawn);

      /* Initialize boards with existing board cards */
      board1 = board;
      board2 = board;

      /* Draw random cards for the two boards */
      for (int j = 0; j < totalCardsNeeded; j++)
      {
        int numAvailable = StdDeck_numCards(availableCards);
        if (numAvailable == 0)
          break;

        int r = rand() % numAvailable;
        int count = 0;

        /* Find the r-th available card */
        for (int c = 0; c < StdDeck_N_CARDS; c++)
        {
          if (StdDeck_CardMask_CARD_IS_SET(availableCards, c))
          {
            if (count == r)
            {
              StdDeck_CardMask_SET(drawn, c);
              StdDeck_CardMask_UNSET(availableCards, c);

              /* Distribute cards: first cardsNeeded go to board1, rest to board2 */
              if (j < cardsNeeded)
              {
                StdDeck_CardMask_SET(board1, c);
              }
              else
              {
                StdDeck_CardMask_SET(board2, c);
              }
              break;
            }
            count++;
          }
        }
      }

      /* Now evaluate using the double flop inner loop */
      {
        int i;
        HandVal hival1[ENUM_MAXPLAYERS], hival2[ENUM_MAXPLAYERS];
        HandVal besthi1 = HandVal_NOTHING, besthi2 = HandVal_NOTHING;
        int hishare1 = 0, hishare2 = 0;
        double hipot1, hipot2;

        /* First pass: evaluate all hands */
        for (i = 0; i < npockets; i++)
        {
          StdDeck_CardMask _hand1, _hand2;
          StdDeck_CardMask_OR(_hand1, pockets[i], board1);
          StdDeck_CardMask_OR(_hand2, pockets[i], board2);
          hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
          hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
        }

        /* Find best hands for both boards */
        for (i = 0; i < npockets; i++)
        {
          if (hival1[i] > besthi1)
          {
            besthi1 = hival1[i];
            hishare1 = 1;
          }
          else if (hival1[i] == besthi1)
          {
            hishare1++;
          }
          if (hival2[i] > besthi2)
          {
            besthi2 = hival2[i];
            hishare2 = 1;
          }
          else if (hival2[i] == besthi2)
          {
            hishare2++;
          }
        }

        /* Pot distribution for each board */
        hipot1 = besthi1 != HandVal_NOTHING ? 1.0 / hishare1 : 0;
        hipot2 = besthi2 != HandVal_NOTHING ? 1.0 / hishare2 : 0;

        /* Award pots and update statistics */
        for (i = 0; i < npockets; i++)
        {
          double potfrac = 0;
          int wins = 0, ties = 0, losses = 0;

          /* Board 1 results */
          if (hival1[i] == besthi1)
          {
            potfrac += hipot1 / 2; /* Half pot from board 1 */
            if (hishare1 == 1)
              wins++;
            else
              ties++;
          }
          else
          {
            losses++;
          }

          /* Board 2 results */
          if (hival2[i] == besthi2)
          {
            potfrac += hipot2 / 2; /* Half pot from board 2 */
            if (hishare2 == 1)
              wins++;
            else
              ties++;
          }
          else
          {
            losses++;
          }

          /* Update EV */
          result->ev[i] += potfrac;

          /* Update win/tie/loss counters based on overall result */
          if (wins == 2)
          {
            /* Won both boards outright */
            result->nwinhi[i]++;
            result->nscoop[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d won both boards (iter %d)\n", i, iter);
          }
          else if (wins == 1 && ties == 1)
          {
            /* Won one, tied one */
            result->nwinhi[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d won one, tied one (iter %d)\n", i, iter);
          }
          else if (wins == 1 && losses == 1)
          {
            /* Won one, lost one - this is a tie overall */
            result->ntiehi[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d won one, lost one = tie (iter %d)\n", i, iter);
          }
          else if (ties == 2)
          {
            /* Tied both boards */
            result->ntiehi[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d tied both boards (iter %d)\n", i, iter);
          }
          else if (ties == 1 && losses == 1)
          {
            /* Tied one, lost one */
            result->nlosehi[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d tied one, lost one (iter %d)\n", i, iter);
          }
          else if (losses == 2)
          {
            /* Lost both boards */
            result->nlosehi[i]++;
            if (iter < 5)
              fprintf(stderr, "DEBUG: Player %d lost both boards (iter %d)\n", i, iter);
          }
        }

        /* Increment samples counter for this iteration */
        result->nsamples++;
      }
    }
  }
  else
  {
    return 1;
  }

  result->game = game;
  result->nplayers = npockets;
  result->sampleType = ENUM_SAMPLE;

  /* Debug output for double flop */
  if (game == game_doubleflop_holdem)
  {
    fprintf(stderr, "\nDEBUG: Final statistics for double flop:\n");
    for (int i = 0; i < npockets; i++)
    {
      fprintf(stderr, "Player %d: wins=%d, losses=%d, ties=%d, scoops=%d, ev=%.3f\n",
              i, result->nwinhi[i], result->nlosehi[i], result->ntiehi[i],
              result->nscoop[i], result->ev[i]);
    }
    fprintf(stderr, "Total samples: %d\n\n", result->nsamples);
  }

  return 0;
}

void enumResultClear(enum_result_t *result)
{
  memset(result, 0, sizeof(enum_result_t));
}

/* NOTE: this memsets the struct, which drops result->ordering without freeing
 * it. That is deliberate — it doubles as the initializer for a fresh, still
 * uninitialized enum_result_t, so it cannot dereference the old pointer. Call
 * enumResultFree() first if the result may already own an ordering. The
 * enumeration entry points below clear the caller's result on entry and
 * allocate their own ordering, so callers should hand them a cleared struct
 * rather than a pre-allocated one. */
void enumResultClear(enum_result_t *result);

void enumResultFree(enum_result_t *result)
{
  if (result->ordering != NULL)
  {
    if (result->ordering->hist != NULL)
      free(result->ordering->hist);
    free(result->ordering);
    result->ordering = NULL;
  }
}

int enumResultAlloc(enum_result_t *result, int nplayers,
                    enum_ordering_mode_t mode)
{
  int nentries;
  switch (mode)
  {
  case enum_ordering_mode_hi:
  case enum_ordering_mode_lo:
    nentries = ENUM_ORDERING_NENTRIES(nplayers);
    break;
  case enum_ordering_mode_hilo:
    nentries = ENUM_ORDERING_NENTRIES_HILO(nplayers);
    break;
  case enum_ordering_mode_hihi:
    /* For double flop, we need space for all combinations of ranks on both boards */
    nentries = ENUM_ORDERING_NENTRIES(nplayers) * ENUM_ORDERING_NENTRIES(nplayers);
    break;
  case enum_ordering_mode_none:
    return 0;
  default:
    return 1;
  }
  if (nentries <= 0)
    return 1;
  result->ordering = (enum_ordering_t *)malloc(sizeof(enum_ordering_t));
  if (result->ordering == NULL)
    return 1;
  result->ordering->mode = mode;
  result->ordering->nplayers = nplayers;
  result->ordering->nentries = nentries;
  result->ordering->hist = (unsigned int *)calloc(nentries, sizeof(int));
  if (result->ordering->hist == NULL)
  {
    free(result->ordering);
    result->ordering = NULL;
    return 1;
  }
  return 0;
}

enum_gameparams_t *
enumGameParams(enum_game_t game)
{
  if (game >= 0 && game < game_NUMGAMES)
    return &enum_gameparams[game];
  else
    return NULL;
}

static void
enumResultPrintOrdering(enum_result_t *result, int terse)
{
  int i, k;

  if (!terse)
    printf("Histogram of relative hand ranks:\n");
  if (result->ordering->mode == enum_ordering_mode_hi ||
      result->ordering->mode == enum_ordering_mode_lo)
  {
    if (!terse)
    {
      for (k = 0; k < result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf(" %8s\n", "Freq");
    }
    else
      printf("ORD %d %d:", result->ordering->mode, result->ordering->nplayers);
    for (i = 0; i < result->ordering->nentries; i++)
    {
      if (result->ordering->hist[i] > 0)
      {
        for (k = 0; k < result->ordering->nplayers; k++)
        {
          int rank = ENUM_ORDERING_DECODE_K(i,
                                            result->ordering->nplayers, k);
          if (rank == result->ordering->nplayers)
            printf(" NQ");
          else
          {
            printf(" %2d", rank + 1);
          }
        }
        printf(" %8d", result->ordering->hist[i]);
        printf(terse ? "|" : "\n");
      }
    }
  }
  else if (result->ordering->mode == enum_ordering_mode_hilo)
  {
    if (!terse)
    {
      printf("HI:");
      for (k = 0; k < result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf("  LO:");
      for (k = 0; k < result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf(" %8s\n", "Freq");
    }
    else
      printf("ORD %d %d:", result->ordering->mode, result->ordering->nplayers);
    for (i = 0; i < result->ordering->nentries; i++)
    {
      if (result->ordering->hist[i] > 0)
      {
        if (!terse)
          printf("   ");
        for (k = 0; k < result->ordering->nplayers; k++)
        {
          int rankhi = ENUM_ORDERING_DECODE_HILO_K_HI(i,
                                                      result->ordering->nplayers, k);
          if (rankhi == result->ordering->nplayers)
            printf(" NQ");
          else
          {
            printf(" %2d", rankhi + 1);
          }
        }
        if (!terse)
          printf("     ");
        for (k = 0; k < result->ordering->nplayers; k++)
        {
          int ranklo = ENUM_ORDERING_DECODE_HILO_K_LO(i,
                                                      result->ordering->nplayers, k);
          if (ranklo == result->ordering->nplayers)
            printf(" NQ");
          else
          {
            printf(" %2d", ranklo + 1);
          }
        }
        printf(" %8d", result->ordering->hist[i]);
        printf(terse ? "|" : "\n");
      }
    }
  }
  else if (result->ordering->mode == enum_ordering_mode_hihi)
  {
    if (!terse)
    {
      printf("Board1:");
      for (k = 0; k < result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf("  Board2:");
      for (k = 0; k < result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf(" %8s\n", "Freq");
    }
    else
      printf("ORD %d %d:", result->ordering->mode, result->ordering->nplayers);

    for (i = 0; i < result->ordering->nentries; i++)
    {
      if (result->ordering->hist[i] > 0)
      {
        int nplayers = result->ordering->nplayers;
        int rank1_base = i / ENUM_ORDERING_NENTRIES(nplayers);
        int rank2_base = i % ENUM_ORDERING_NENTRIES(nplayers);

        if (!terse)
          printf("      ");

        /* Print ranks for board 1 */
        for (k = 0; k < nplayers; k++)
        {
          int rank1 = ENUM_ORDERING_DECODE_K(rank1_base, nplayers, k);
          if (rank1 == nplayers)
            printf(" NQ");
          else
            printf(" %2d", rank1 + 1);
        }

        if (!terse)
          printf("        ");

        /* Print ranks for board 2 */
        for (k = 0; k < nplayers; k++)
        {
          int rank2 = ENUM_ORDERING_DECODE_K(rank2_base, nplayers, k);
          if (rank2 == nplayers)
            printf(" NQ");
          else
            printf(" %2d", rank2 + 1);
        }

        printf(" %8d", result->ordering->hist[i]);
        printf(terse ? "|" : "\n");
      }
    }
  }
  if (terse)
    printf("\n");
}

void enumResultPrint(enum_result_t *result, StdDeck_CardMask pockets[],
                     StdDeck_CardMask board)
{
  int i;
  enum_gameparams_t *gp;
  int width;

  gp = enumGameParams(result->game);
  if (gp == NULL)
  {
    printf("enumResultPrint: invalid game type\n");
    return;
  }
  width = gp->maxpocket * 3 - 1;
  printf("%s: %d %s %s%s", gp->name, result->nsamples,
         (result->sampleType == ENUM_SAMPLE) ? "sampled" : "enumerated",
         (gp->maxboard > 0) ? "board" : "outcome",
         (result->nsamples == 1 ? "" : "s"));
  if (!StdDeck_CardMask_IS_EMPTY(board))
    printf(" containing %s", DmaskString(StdDeck, board));
  printf("\n");

  if (gp->haslopot && gp->hashipot)
  {
    printf("%*s %7s   %7s %7s %7s   %7s %7s %7s   %5s\n",
           -width, "cards", "scoop",
           "HIwin", "HIlos", "HItie",
           "LOwin", "LOlos", "LOtie",
           "EV");
    for (i = 0; i < result->nplayers; i++)
    {
      printf("%*s %7d   %7d %7d %7d   %7d %7d %7d   %5.3f\n",
             -width, DmaskString(StdDeck, pockets[i]), result->nscoop[i],
             result->nwinhi[i], result->nlosehi[i], result->ntiehi[i],
             result->nwinlo[i], result->nloselo[i], result->ntielo[i],
             result->ev[i] / result->nsamples);
    }
#if 0
    {
    int j;
    /* experimental output format to show pot splitting */
    printf("\n%*s", -width, "cards");
    for (j=0; j<=result->nplayers; j++)
      printf(" %6s%d", "HI", j);
    for (j=0; j<=result->nplayers; j++)
      printf(" %6s%d", "LO", j);
    printf("\n");
    for (i=0; i<result->nplayers; i++) {
      printf("%*s", -width, DmaskString(StdDeck, pockets[i]));
      for (j=0; j<=result->nplayers; j++)
        printf(" %7d", result->nsharehi[i][j]);
      for (j=0; j<=result->nplayers; j++)
        printf(" %7d", result->nsharelo[i][j]);
      printf("\n");
    }
    }
#endif
  }
  else
  {
    printf("%*s %7s %6s   %7s %6s   %7s %6s     %5s\n",
           -width, "cards", "win", "%win", "lose", "%lose", "tie", "%tie", "EV");
    if (gp->haslopot)
    {
      for (i = 0; i < result->nplayers; i++)
      {
        printf("%*s %7d %6.2f   %7d %6.2f   %7d %6.2f     %5.3f\n",
               -width, DmaskString(StdDeck, pockets[i]),
               result->nwinlo[i], 100.0 * result->nwinlo[i] / result->nsamples,
               result->nloselo[i], 100.0 * result->nloselo[i] / result->nsamples,
               result->ntielo[i], 100.0 * result->ntielo[i] / result->nsamples,
               result->ev[i] / result->nsamples);
      }
    }
    else if (gp->hashipot)
    {
      for (i = 0; i < result->nplayers; i++)
      {
        printf("%*s %7d %6.2f   %7d %6.2f   %7d %6.2f     %5.3f\n",
               -width, DmaskString(StdDeck, pockets[i]),
               result->nwinhi[i], 100.0 * result->nwinhi[i] / result->nsamples,
               result->nlosehi[i], 100.0 * result->nlosehi[i] / result->nsamples,
               result->ntiehi[i], 100.0 * result->ntiehi[i] / result->nsamples,
               result->ev[i] / result->nsamples);
      }
    }
  }

  if (result->ordering != NULL)
    enumResultPrintOrdering(result, 0);
}

void enumResultPrintTerse(enum_result_t *result, StdDeck_CardMask pockets[],
                          StdDeck_CardMask board)
{
  int i;

  printf("EV %d:", result->nplayers);
  for (i = 0; i < result->nplayers; i++)
    printf(" %8.6f", result->ev[i] / result->nsamples);
  printf("\n");
  if (result->ordering != NULL)
    enumResultPrintOrdering(result, 1);
}
