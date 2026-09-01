/*
 *
 * Copyright (C) 2007, 2008 Loic Dachary <loic@dachary.org>
 * Copyright (C) 2004, 2005, 2006 Mekensleep
 *
 *	Mekensleep
 *	24 rue vieille du temple
 *	75004 Paris
 *       licensing@mekensleep.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Authors:
 *  Loic Dachary <loic@dachary.org>
 *
 */

#ifdef _DEBUG // for Windows python23_d.lib is not in distribution... ugly but works
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

#include <math.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#endif

/* enumerate.c -- functions to compute pot equity by enumerating outcomes
  Exports:
        enumExhaustive()	exhaustive enumeration of outcomes
        enumGameParams()	look up rule parameters by game type
        enumResultClear()	clear enumeration result object
        enumResultPrint()	print enumeration result object
        enumResultPrintTerse()	print enumeration result object, tersely
        enumSample()		monte carlo sampling of outcomes

   Michael Maurer, Apr 2002
*/

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/games/game_std.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/games/eval_low.h>
#include <poker_eval/games/eval_low8.h>
#include <poker_eval/games/eval_joker_low.h>
#include <poker_eval/games/eval_joker_low8.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>

#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/distributions/omaha_distributions.h> // Added for Omaha hand instantiation
#include <poker_eval/distributions/stud_distributions.h>  // Added for Stud hand instantiation
#include <poker_eval/equity/RangeEquity.h>                // Added for Range Equity functionality
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_traversal.h>
#include <stdbool.h>

#ifdef WIN32
#define VERSION_NAME(W) W##3_11
#define PYTHON_VERSION "3_11"
#endif /* WIN32 */

/* The 8-or-better low of the hi/lo variants (holdem8, 7stud8).  Goes through
   the dedicated 8-or-better evaluator, as the library itself does in
   INNER_LOOP_HOLDEM8 / INNER_LOOP_7STUD8: it keeps only the five lowest
   distinct ranks and returns NOTHING when the hand does not qualify. */
static inline LowHandVal py_eval_low8_qualified(StdDeck_CardMask cards)
{
  LowHandVal value = StdDeck_Lowball8_EVAL(cards, StdDeck_numCards(cards));
  return pe_low_qualify5(value, LOW_QUALIFIER_8) ? value : LowHandVal_NOTHING;
}

static inline LowHandVal py_eval_low_a5_raw(StdDeck_CardMask cards)
{
  return pe_eval_low_a5(cards);
}

static inline LowHandVal py_eval_low8_joker_qualified(JokerDeck_CardMask cards)
{
  LowHandVal value = JokerDeck_Lowball8_EVAL(cards, 5);
  return pe_low_qualify5(value, LOW_QUALIFIER_8) ? value : LowHandVal_NOTHING;
}

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

#define INNER_LOOP(evalwrap)                       \
  do                                               \
  {                                                \
    int i;                                         \
    HandVal hival[ENUM_MAXPLAYERS];                \
    LowHandVal loval[ENUM_MAXPLAYERS];             \
    HandVal besthi = HandVal_NOTHING;              \
    LowHandVal bestlo = LowHandVal_NOTHING;        \
    int hishare = 0;                               \
    int loshare = 0;                               \
    double hipot, lopot;                           \
    /* find winning hands for high and low */      \
    for (i = 0; i < sizeToDeal - 1; i++)           \
    {                                              \
      int err;                                     \
      {                                            \
        evalwrap                                   \
      }                                            \
      if (err != 0)                                \
        return 1000 + err;                         \
      if (hival[i] != HandVal_NOTHING)             \
      {                                            \
        if (hival[i] > besthi)                     \
        {                                          \
          besthi = hival[i];                       \
          hishare = 1;                             \
        }                                          \
        else if (hival[i] == besthi)               \
        {                                          \
          hishare++;                               \
        }                                          \
      }                                            \
      if (loval[i] != LowHandVal_NOTHING)          \
      {                                            \
        if (loval[i] < bestlo)                     \
        {                                          \
          bestlo = loval[i];                       \
          loshare = 1;                             \
        }                                          \
        else if (loval[i] == bestlo)               \
        {                                          \
          loshare++;                               \
        }                                          \
      }                                            \
    }                                              \
    /* now award pot fractions to winning hands */ \
    if (bestlo != LowHandVal_NOTHING &&            \
        besthi != HandVal_NOTHING)                 \
    {                                              \
      hipot = 0.5 / hishare;                       \
      lopot = 0.5 / loshare;                       \
    }                                              \
    else if (bestlo == LowHandVal_NOTHING &&       \
             besthi != HandVal_NOTHING)            \
    {                                              \
      hipot = 1.0 / hishare;                       \
      lopot = 0;                                   \
    }                                              \
    else if (bestlo != LowHandVal_NOTHING &&       \
             besthi == HandVal_NOTHING)            \
    {                                              \
      hipot = 0;                                   \
      lopot = 1.0 / loshare;                       \
    }                                              \
    else                                           \
    {                                              \
      hipot = lopot = 0;                           \
    }                                              \
    for (i = 0; i < sizeToDeal - 1; i++)           \
    {                                              \
      double potfrac = 0;                          \
      int H = 0, L = 0;                            \
      if (hival[i] != HandVal_NOTHING)             \
      {                                            \
        if (hival[i] == besthi)                    \
        {                                          \
          H = hishare;                             \
          potfrac += hipot;                        \
          if (hishare == 1)                        \
            result->nwinhi[i]++;                   \
          else                                     \
            result->ntiehi[i]++;                   \
        }                                          \
        else                                       \
        {                                          \
          result->nlosehi[i]++;                    \
        }                                          \
      }                                            \
      if (loval[i] != LowHandVal_NOTHING)          \
      {                                            \
        if (loval[i] == bestlo)                    \
        {                                          \
          L = loshare;                             \
          potfrac += lopot;                        \
          if (loshare == 1)                        \
            result->nwinlo[i]++;                   \
          else                                     \
            result->ntielo[i]++;                   \
        }                                          \
        else                                       \
        {                                          \
          result->nloselo[i]++;                    \
        }                                          \
      }                                            \
      result->nsharehi[i][H]++;                    \
      result->nsharelo[i][L]++;                    \
      result->nshare[i][H][L]++;                   \
      if (potfrac > 0.99)                          \
        result->nscoop[i]++;                       \
      result->ev[i] += potfrac;                    \
    }                                              \
    result->nsamples++;                            \
  } while (0);

#define INNER_LOOP_ANY_HIGH                                 \
  INNER_LOOP({                                              \
    StdDeck_CardMask _hand;                                 \
    StdDeck_CardMask _finalBoard;                           \
    StdDeck_CardMask_RESET(_hand);                          \
    StdDeck_CardMask_RESET(_finalBoard);                    \
    StdDeck_CardMask_OR(_finalBoard, board, cardsDealt[0]); \
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);    \
    StdDeck_CardMask_OR(_hand, _hand, cardsDealt[i + 1]);   \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);           \
    loval[i] = LowHandVal_NOTHING;                          \
    err = 0;                                                \
  })

#define INNER_LOOP_ANY_HILO                                 \
  INNER_LOOP({                                              \
    StdDeck_CardMask _hand;                                 \
    StdDeck_CardMask _finalBoard;                           \
    StdDeck_CardMask_RESET(_hand);                          \
    StdDeck_CardMask_RESET(_finalBoard);                    \
    StdDeck_CardMask_OR(_finalBoard, board, cardsDealt[0]); \
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);    \
    StdDeck_CardMask_OR(_hand, _hand, cardsDealt[i + 1]);   \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);           \
    loval[i] = py_eval_low8_qualified(_hand);               \
    err = 0;                                                \
  })

#define INNER_LOOP_OMAHA                                       \
  INNER_LOOP({                                                 \
    StdDeck_CardMask _hand;                                    \
    StdDeck_CardMask _finalBoard;                              \
    StdDeck_CardMask_RESET(_hand);                             \
    StdDeck_CardMask_RESET(_finalBoard);                       \
    StdDeck_CardMask_OR(_finalBoard, board, cardsDealt[0]);    \
    StdDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    err = StdDeck_OmahaHiLow8_EVAL(_hand, _finalBoard,         \
                                   &hival[i], NULL);           \
    loval[i] = LowHandVal_NOTHING;                             \
  })

#define INNER_LOOP_OMAHA8                                      \
  INNER_LOOP({                                                 \
    StdDeck_CardMask _hand;                                    \
    StdDeck_CardMask _finalBoard;                              \
    StdDeck_CardMask_RESET(_hand);                             \
    StdDeck_CardMask_RESET(_finalBoard);                       \
    StdDeck_CardMask_OR(_finalBoard, board, cardsDealt[0]);    \
    StdDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    err = StdDeck_OmahaHiLow8_EVAL(_hand, _finalBoard,         \
                                   &hival[i], &loval[i]);      \
    if (err != 0 || !pe_low_qualify5(loval[i], LOW_QUALIFIER_8)) \
      loval[i] = LowHandVal_NOTHING;                          \
  })

#define INNER_LOOP_7STUDNSQ                                    \
  INNER_LOOP({                                                 \
    StdDeck_CardMask _hand;                                    \
    StdDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);              \
    loval[i] = py_eval_low_a5_raw(_hand);                      \
    err = 0;                                                   \
  })

#define INNER_LOOP_RAZZ                                        \
  INNER_LOOP({                                                 \
    StdDeck_CardMask _hand;                                    \
    StdDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = HandVal_NOTHING;                                \
    loval[i] = py_eval_low_a5_raw(_hand);                      \
    err = 0;                                                   \
  })

#define INNER_LOOP_5DRAW                                         \
  INNER_LOOP({                                                   \
    JokerDeck_CardMask _hand;                                    \
    JokerDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);            \
    loval[i] = LowHandVal_NOTHING;                               \
    err = 0;                                                     \
  })

#define INNER_LOOP_5DRAW8                                        \
  INNER_LOOP({                                                   \
    JokerDeck_CardMask _hand;                                    \
    JokerDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);            \
    loval[i] = py_eval_low8_joker_qualified(_hand);              \
    err = 0;                                                     \
  })

#define INNER_LOOP_5DRAWNSQ                                      \
  INNER_LOOP({                                                   \
    JokerDeck_CardMask _hand;                                    \
    JokerDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);            \
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);                 \
    err = 0;                                                     \
  })

#define INNER_LOOP_LOWBALL                                       \
  INNER_LOOP({                                                   \
    JokerDeck_CardMask _hand;                                    \
    JokerDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = HandVal_NOTHING;                                  \
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);                 \
    err = 0;                                                     \
  })

#define INNER_LOOP_LOWBALL27                                   \
  INNER_LOOP({                                                 \
    StdDeck_CardMask _hand;                                    \
    StdDeck_CardMask_OR(_hand, pockets[i], cardsDealt[i + 1]); \
    hival[i] = HandVal_NOTHING;                                \
    loval[i] = StdDeck_StdRules_EVAL_N(_hand, 5);              \
    err = 0;                                                   \
  })

static int
pyenumExhaustive(enum_game_t game, StdDeck_CardMask pockets[],
                 int numToDeal[],
                 StdDeck_CardMask board, StdDeck_CardMask dead,
                 int sizeToDeal, enum_result_t *result)
{
  int totalToDeal = 0;
  int i;
  enumResultClear(result);
  StdDeck_CardMask cardsDealt[ENUM_MAXPLAYERS + 1];
  memset(cardsDealt, 0, sizeof(StdDeck_CardMask) * (ENUM_MAXPLAYERS + 1));
  if (sizeToDeal - 1 > ENUM_MAXPLAYERS)
    return 1;
  for (i = 0; i < sizeToDeal; i++)
    totalToDeal += numToDeal[i];

  /*
   * Cards in pockets or in the board must not be dealt
   */
  StdDeck_CardMask_OR(dead, dead, board);
  for (i = 0; i < sizeToDeal - 1; i++)
  {
    StdDeck_CardMask_OR(dead, dead, pockets[i]);
  }

  if (game == game_holdem)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_ANY_HIGH);
    }
    else
    {
      INNER_LOOP_ANY_HIGH;
    }
  }
  else if (game == game_holdem8)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_ANY_HILO);
    }
    else
    {
      INNER_LOOP_ANY_HILO;
    }
  }
  else if (game == game_omaha)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_OMAHA);
    }
    else
    {
      INNER_LOOP_OMAHA;
    }
  }
  else if (game == game_omaha8)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_OMAHA8);
    }
    else
    {
      INNER_LOOP_OMAHA8;
    }
  }
  else if (game == game_7stud)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_ANY_HIGH);
    }
    else
    {
      INNER_LOOP_ANY_HIGH;
    }
  }
  else if (game == game_7stud8)
  {
    if (totalToDeal > 0)
    {
      DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                    sizeToDeal, numToDeal,
                                    dead, INNER_LOOP_ANY_HILO);
    }
    else
    {
      INNER_LOOP_ANY_HILO;
    }
  }
  else if (game == game_7studnsq)
  {
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                  sizeToDeal, numToDeal,
                                  dead, INNER_LOOP_7STUDNSQ);
  }
  else if (game == game_razz)
  {
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                  sizeToDeal, numToDeal,
                                  dead, INNER_LOOP_RAZZ);
  }
  else if (game == game_lowball27)
  {
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                  sizeToDeal, numToDeal,
                                  dead, INNER_LOOP_LOWBALL27);
  }
  else if (game == game_lowball)
  {
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, cardsDealt,
                                  sizeToDeal, numToDeal,
                                  dead, INNER_LOOP_LOWBALL);
  }
  else
  {
    return 1;
  }

  result->game = game;
  result->nplayers = sizeToDeal - 1;
  result->sampleType = ENUM_EXHAUSTIVE;
  return 0;
}

static int
pyenumSample(enum_game_t game, StdDeck_CardMask pockets[],
             int numToDeal[],
             StdDeck_CardMask board, StdDeck_CardMask dead,
             int sizeToDeal, int iterations, enum_result_t *result)
{
  int i;
  enumResultClear(result);
  StdDeck_CardMask cardsDealt[ENUM_MAXPLAYERS + 1];
  memset(cardsDealt, 0, sizeof(StdDeck_CardMask) * (ENUM_MAXPLAYERS + 1));
  if (sizeToDeal - 1 > ENUM_MAXPLAYERS)
    return 1;

  /*
   * Cards in pockets or in the board must not be dealt
   */
  StdDeck_CardMask_OR(dead, dead, board);
  for (i = 0; i < sizeToDeal - 1; i++)
  {
    StdDeck_CardMask_OR(dead, dead, pockets[i]);
  }

  if (game == game_holdem)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_ANY_HIGH);
  }
  else if (game == game_holdem8)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_ANY_HILO);
  }
  else if (game == game_omaha)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_OMAHA);
  }
  else if (game == game_omaha8)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_OMAHA8);
  }
  else if (game == game_7stud)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_ANY_HIGH);
  }
  else if (game == game_7stud8)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_ANY_HILO);
  }
  else if (game == game_7studnsq)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_7STUDNSQ);
  }
  else if (game == game_razz)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_RAZZ);
  }
  else if (game == game_lowball27)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_LOWBALL27);
  }
  else if (game == game_lowball)
  {
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, cardsDealt,
                                   sizeToDeal, numToDeal,
                                   dead, iterations, INNER_LOOP_LOWBALL);
  }
  else
  {
    return 1;
  }

  result->game = game;
  result->nplayers = sizeToDeal - 1;
  result->sampleType = ENUM_SAMPLE;
  return 0;
}

#define NOCARD 255

static int PyList2CardMask(PyObject *object, CardMask *cardsp)
{
  CardMask cards;
  int cards_size = 0;
  int valid_cards_size = 0;

  if (!PyList_Check(object))
  {
    PyErr_SetString(PyExc_TypeError, "expected a list of cards");
    return -1;
  }

  valid_cards_size = cards_size = (int)PyList_Size(object);
  CardMask_RESET(cards);

  int card;
  int i;
  for (i = 0; i < cards_size; i++)
  {
    card = -1;
    PyObject *pycard = PyList_GetItem(object, i);
    if (PyErr_Occurred())
      return -1;

    if (PyUnicode_Check(pycard))
    {
      const char *card_string = PyUnicode_AsUTF8(pycard);
      if (!strcmp(strdup(card_string), "__"))
      {
        card = 255;
      }
      else
      {
        if (Deck_stringToCard(strdup(card_string), &card) == 0)
        {
          PyErr_Format(PyExc_RuntimeError, "card %s is not a valid card name", strdup(card_string));
          return -1;
        }
      }
    }
    else if (PyLong_Check(pycard))
    {
      card = (int)PyLong_AsLong(pycard);
      if (card != NOCARD && (card < 0 || card > StdDeck_N_CARDS))
      {
        PyErr_Format(PyExc_TypeError, "card value (%d) must be in the range [0-%d]", card, StdDeck_N_CARDS);
        return -1;
      }
    }
    else
    {
      PyErr_SetString(PyExc_TypeError, "card must be a string or an int");
      return -1;
    }

    if (card == NOCARD)
      valid_cards_size--;
    else
      CardMask_SET(cards, card);
  }

  *cardsp = cards;

  return valid_cards_size;
}

static PyObject *CardMask2PyList(CardMask *cardmask)
{
  PyObject *result = 0;
  int cardmask_size = StdDeck_numCards(cardmask);
  int cards[64];
  int i;

  if ((i = CurDeck.maskToCards(cardmask, cards)) != cardmask_size)
  {
    PyErr_Format(PyExc_RuntimeError, "CardMask2PyList: maskToCards returns %d cards, expected %d\n", i, cardmask_size);
    return 0;
  }

  result = PyList_New(0);
  for (i = 0; i < cardmask_size; i++)
  {
    PyObject *pycard = Py_BuildValue("i", cards[i]);
    int status = PyList_Append(result, pycard);
    Py_DECREF(pycard);
    if (status < 0)
      return 0;
  }

  return result;
}

static char doc_poker_evaln[] =
    "EvalN";

static PyObject *
poker_evaln(PyObject *self, PyObject *args)
{
  CardMask cards;
  PyObject *pycards;
  HandVal handval;

  if (!PyArg_ParseTuple(args, "O", &pycards))
    return NULL;

  if (PyList2CardMask(pycards, &cards) < 0)
    return NULL;

  handval = Hand_EVAL_N(cards, (int)PyList_Size(pycards));

  return Py_BuildValue("i", handval);
}

static char doc_string2card[] =
    "return the numerical representation of a card";

static PyObject *
string2card(PyObject *self, PyObject *args)
{
  char *card_string = 0;

  if (!PyArg_ParseTuple(args, "s", &card_string))
    return NULL;

  {
    int card = 0;

    if (!strcmp(card_string, "__"))
    {
      card = 255;
    }
    else
    {
      if (Deck_stringToCard(card_string, &card) == 0)
      {
        PyErr_Format(PyExc_RuntimeError, "card %s is not a valid card name", card_string);
        return NULL;
      }
    }

    return Py_BuildValue("b", (unsigned char)card);
  }
}

static char doc_card2string[] =
    "return the string representation of a numerical card";

static PyObject *
card2string(PyObject *self, PyObject *args)
{
  int card = 0;

  if (!PyArg_ParseTuple(args, "i", &card))
    return NULL;

  if (card == 255)
  {
    return Py_BuildValue("s", "__");
  }
  else
  {
    /*
     * Avoid using GenericDeck_cardString as long as it insists
     * on using the "static thread" hack (see lib/deck.c).
     */
    char tmp[16];
    StdDeck.cardToString(card, tmp);
    return Py_BuildValue("s", tmp);
  }
}

/*
 * Find the card with highest suit matching rank in hand
 * and remove it from hand. The removed card is returned.
 */
static int findanddelete(CardMask *hand, int rank)
{
  int suit;
  for (suit = StdDeck_Suit_LAST; suit >= StdDeck_Suit_FIRST; suit--)
  {
    int card = StdDeck_MAKE_CARD(rank, suit);
    if (CardMask_CARD_IS_SET(*hand, card))
    {
      CardMask_UNSET(*hand, card);
      return card;
    }
  }
  return -1;
}

#define LOWRANK2RANK(c) ((c) == StdDeck_Rank_2 ? StdDeck_Rank_ACE : (c - 1))

static PyObject *
CardMask2SortedPyList(CardMask hand, int low)
{
  int i;
  HandVal handval;
  PyObject *result = PyList_New(0);

  if (StdDeck_CardMask_IS_EMPTY(hand))
  {
    PyObject *pyvalue = Py_BuildValue("s", "Nothing");
    PyList_Append(result, pyvalue);
    Py_DECREF(pyvalue);
    return result;
  }

  if (low)
  {
    LowHandVal low_val = pe_eval_low_a5(hand);
    if (pe_low_qualify5(low_val, LOW_QUALIFIER_8))
      handval = low_val;
    else
      handval = LowHandVal_NOTHING;
  }
  else
  {
    handval = Hand_EVAL_N(hand, 5);
  }

  int htype = HandVal_HANDTYPE(handval);
  {
    PyObject *pyvalue = Py_BuildValue("s", StdRules_handTypeNames[htype]);
    PyList_Append(result, pyvalue);
    Py_DECREF(pyvalue);
  }

  if (!low || htype != LowHandVal_NOTHING)
  {
    if (StdRules_nSigCards[htype] >= 1)
    {
      int rank = HandVal_TOP_CARD(handval);
      if (low)
        rank = LOWRANK2RANK(rank);

      if (htype == HandType_STRAIGHT || htype == HandType_STFLUSH)
      {
        for (i = rank; rank - i < 5; i--)
        {
          int rank_modulo = i < 0 ? StdDeck_Rank_ACE : i;
          PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank_modulo));
          PyList_Append(result, pyvalue);
          Py_DECREF(pyvalue);
        }
      }
      else
      {
        int count;
        switch (htype)
        {
        case HandType_ONEPAIR:
        case HandType_TWOPAIR:
          count = 2;
          break;
        case HandType_TRIPS:
        case HandType_FULLHOUSE:
          count = 3;
          break;
        case HandType_QUADS:
          count = 4;
          break;
        default:
          count = 1;
          break;
        }
        for (i = 0; i < count; i++)
        {
          PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank));
          PyList_Append(result, pyvalue);
          Py_DECREF(pyvalue);
        }
      }
    }
    if (StdRules_nSigCards[htype] >= 2)
    {
      int rank = HandVal_SECOND_CARD(handval);
      int count = 1;
      if (low)
        rank = LOWRANK2RANK(rank);
      if (htype == HandType_TWOPAIR ||
          htype == HandType_FULLHOUSE)
        count = 2;

      for (i = 0; i < count; i++)
      {
        PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank));
        PyList_Append(result, pyvalue);
        Py_DECREF(pyvalue);
      }
    }

    if (StdRules_nSigCards[htype] >= 3)
    {
      int rank = HandVal_THIRD_CARD(handval);
      if (low)
        rank = LOWRANK2RANK(rank);
      PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank));
      PyList_Append(result, pyvalue);
      Py_DECREF(pyvalue);
    }

    if (StdRules_nSigCards[htype] >= 4)
    {
      int rank = HandVal_FOURTH_CARD(handval);
      if (low)
        rank = LOWRANK2RANK(rank);
      PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank));
      PyList_Append(result, pyvalue);
      Py_DECREF(pyvalue);
    }

    if (StdRules_nSigCards[htype] >= 5)
    {
      int rank = HandVal_FIFTH_CARD(handval);
      if (low)
        rank = LOWRANK2RANK(rank);
      PyObject *pyvalue = Py_BuildValue("i", findanddelete(&hand, rank));
      PyList_Append(result, pyvalue);
      Py_DECREF(pyvalue);
    }
  }

  /*
   * Append remaining cards, highest first
   */
  for (i = Deck_N_CARDS - 1; i >= 0; i--)
  {
    if (StdDeck_CardMask_CARD_IS_SET(hand, i))
    {
      PyObject *pyvalue = Py_BuildValue("i", i);
      PyList_Append(result, pyvalue);
      Py_DECREF(pyvalue);
    }
  }

  return result;
}

/* Evaluate an omaha hand for both high and low.  Return nonzero on error.
   If hival is NULL, skips high evaluation; if loval is NULL, skips
   low evaluation.  Low eval could be sped up with 256x256 rank table. */

static int
OmahaHiLow8_Best(StdDeck_CardMask hole, StdDeck_CardMask board,
                 HandVal *hival, LowHandVal *loval,
                 StdDeck_CardMask *hicards, StdDeck_CardMask *locards)
{
  StdDeck_CardMask allcards;
  LowHandVal allval;
  HandVal curhi, besthi;
  LowHandVal curlo, bestlo;
  StdDeck_CardMask hole1[OMAHA_MAXHOLE];
  StdDeck_CardMask board1[OMAHA_MAXBOARD];
  StdDeck_CardMask n1, n2, n3, n4, n5;
  int nhole, nboard;
  int eligible = 0;
  int i, h1, h2, b1, b2, b3;

  /* pluck out individual cards from hole and board masks, save in arrays */
  nhole = nboard = 0;
  for (i = 0; i < StdDeck_N_CARDS; i++)
  {
    if (StdDeck_CardMask_CARD_IS_SET(hole, i))
    {
      if (nhole >= OMAHA_MAXHOLE)
        return 1; /* too many hole cards */
      StdDeck_CardMask_RESET(hole1[nhole]);
      StdDeck_CardMask_SET(hole1[nhole], i);
      nhole++;
    }
    if (StdDeck_CardMask_CARD_IS_SET(board, i))
    {
      if (StdDeck_CardMask_CARD_IS_SET(hole, i)) /* same card in hole and board */
        return 2;
      if (nboard >= OMAHA_MAXBOARD)
        return 3; /* too many board cards */
      StdDeck_CardMask_RESET(board1[nboard]);
      StdDeck_CardMask_SET(board1[nboard], i);
      nboard++;
    }
  }

  if (nhole < OMAHA_MINHOLE || nhole > OMAHA_MAXHOLE)
    return 4; /* wrong # of hole cards */
  if (nboard < OMAHA_MINBOARD || nboard > OMAHA_MAXBOARD)
    return 5; /* wrong # of board cards */

  /* quick test in case no low is possible with all 9 cards */
  if (loval != NULL)
  {
    StdDeck_CardMask_OR(allcards, hole, board);
    allval = pe_eval_low_a5(allcards);
    eligible = pe_low_qualify5(allval, LOW_QUALIFIER_8);
  }

  /* loop over all combinations of hole with board (60 for 4 hole cards
     and 5 board cards). */
  besthi = HandVal_NOTHING;
  bestlo = LowHandVal_NOTHING;
  /* {h1,h2} loop over all hole card combinations */
  for (h1 = 0; h1 < nhole - 1; h1++)
  {
    StdDeck_CardMask_RESET(n1);
    StdDeck_CardMask_OR(n1, n1, hole1[h1]);
    for (h2 = h1 + 1; h2 < nhole; h2++)
    {
      StdDeck_CardMask_OR(n2, n1, hole1[h2]);
      /* {b1,b2,b3} loop over all board card combinations */
      for (b1 = 0; b1 < nboard - 2; b1++)
      {
        StdDeck_CardMask_OR(n3, n2, board1[b1]);
        for (b2 = b1 + 1; b2 < nboard - 1; b2++)
        {
          StdDeck_CardMask_OR(n4, n3, board1[b2]);
          for (b3 = b2 + 1; b3 < nboard; b3++)
          {
            if (hival != NULL)
            {
              StdDeck_CardMask_OR(n5, n4, board1[b3]);
              curhi = StdDeck_StdRules_EVAL_N(n5, 5);
              if (curhi > besthi || besthi == HandVal_NOTHING)
              {
                besthi = curhi;
                *hicards = n5;
              }
            }
            if (loval != NULL && eligible)
            {
              curlo = pe_eval_low_a5(n5);
              if (pe_low_qualify5(curlo, LOW_QUALIFIER_8) &&
                  (curlo < bestlo || bestlo == LowHandVal_NOTHING))
              {
                bestlo = curlo;
                *locards = n5;
              }
            }
          }
        }
      }
    }
  }
  if (hival != NULL)
    *hival = besthi;
  if (loval != NULL)
    *loval = bestlo;
  return 0;
}

static char doc_eval_hand[] =
    "return the evaluation of the hand, either low or hi. Result is a list, first element hand value, second element the best 5 card hand as a list of card values.";

static PyObject *
eval_hand(PyObject *self, PyObject *args)
{
  PyObject *result = 0;
  char *hilow_string = 0;
  PyObject *pyhand = 0;
  PyObject *pyboard = 0;
  int low = 0;
  CardMask hand;
  CardMask board;
  int board_size = 0;
  CardMask best;
  HandVal best_handval;

  StdDeck_CardMask_RESET(best);

  if (!PyArg_ParseTuple(args, "sOO", &hilow_string, &pyhand, &pyboard))
    return NULL;

  if (!strcmp(hilow_string, "low"))
    low = 1;

  if (PyList2CardMask(pyhand, &hand) < 0)
    return NULL;

  board_size = PyList2CardMask(pyboard, &board);

  if (board_size > 0)
  {
    CardMask hicards;
    CardMask locards;
    HandVal hival = 0;
    HandVal loval = 0;
    StdDeck_CardMask_RESET(hicards);
    StdDeck_CardMask_RESET(locards);
    OmahaHiLow8_Best(hand, board, &hival, &loval, &hicards, &locards);
    if (low)
    {
      best_handval = loval;
      if (best_handval != LowHandVal_NOTHING)
        best = locards;
    }
    else
    {
      best = hicards;
      best_handval = hival;
    }
  }
  else
  {
    CardMask cards;
    CardMask dead;

    StdDeck_CardMask_RESET(best);

    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, hand);
    StdDeck_CardMask_NOT(dead, dead);

    if (low)
    {
      best_handval = LowHandVal_NOTHING;
    }
    else
    {
      best_handval = HandVal_NOTHING;
    }

    ENUMERATE_N_CARDS_D(cards, 5, dead,
                        {
                          HandVal handval;

                          if (low)
                          {
                            handval = pe_eval_low_a5(cards);
                            if (!pe_low_qualify5(handval, LOW_QUALIFIER_8))
                              handval = LowHandVal_NOTHING;
                          }
                          else
                          {
                            handval = Hand_EVAL_N(cards, 5);
                          }

                          if (low ? (handval < best_handval) : (handval > best_handval))
                          {
                            best = cards;
                            best_handval = handval;
                          }
                        });
  }

  if (StdDeck_CardMask_IS_EMPTY(best))
  {
    best_handval = low ? 0x0FFFFFFF : 0;
  }

  result = PyList_New(0);
  {
    PyObject *pyvalue = Py_BuildValue("i", best_handval);
    PyList_Append(result, pyvalue);
    Py_DECREF(pyvalue);
  }
  {
    PyObject *pyvalue = CardMask2SortedPyList(best, low);
    PyList_Append(result, pyvalue);
    Py_DECREF(pyvalue);
  }
  return result;
}

static char doc_poker_eval[] =
    "eval a poker game state";

static PyObject *
poker_eval(PyObject *self, PyObject *args, PyObject *keywds)
{
  int i;
  int pockets_size;
  int fill_pockets = 0;
  int iterations = 0;
  PyObject *pypockets = 0;
  PyObject *pyboard = 0;
  char *game = 0;
  PyObject *pydead = 0;
  enum_gameparams_t *params = 0;

  StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
  int numToDeal[ENUM_MAXPLAYERS];
  CardMask dead_cards;
  CardMask board_cards;

  PyObject *result = NULL;

  static const char *kwlist[] = {"game", "pockets", "board", "dead", "fill_pockets", "iterations", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "sOO|Oii", (char **)(void *)kwlist,
                                   &game, &pypockets, &pyboard, &pydead, &fill_pockets, &iterations))
    return NULL;

  if (!strcmp(game, "holdem"))
  {
    params = enumGameParams(game_holdem);
  }
  else if (!strcmp(game, "holdem8"))
  {
    params = enumGameParams(game_holdem8);
  }
  else if (!strcmp(game, "omaha"))
  {
    params = enumGameParams(game_omaha);
  }
  else if (!strcmp(game, "omaha8"))
  {
    params = enumGameParams(game_omaha8);
  }
  else if (!strcmp(game, "7stud"))
  {
    params = enumGameParams(game_7stud);
  }
  else if (!strcmp(game, "7stud8"))
  {
    params = enumGameParams(game_7stud8);
  }
  else if (!strcmp(game, "7studnsq"))
  {
    params = enumGameParams(game_7studnsq);
  }
  else if (!strcmp(game, "razz"))
  {
    params = enumGameParams(game_razz);
  }
  else if (!strcmp(game, "5draw"))
  {
    params = enumGameParams(game_5draw);
  }
  else if (!strcmp(game, "5draw8"))
  {
    params = enumGameParams(game_5draw8);
  }
  else if (!strcmp(game, "5drawnsq"))
  {
    params = enumGameParams(game_5drawnsq);
  }
  else if (!strcmp(game, "lowball"))
  {
    params = enumGameParams(game_lowball);
  }
  else if (!strcmp(game, "lowball27"))
  {
    params = enumGameParams(game_lowball27);
  }
  if (params == 0)
  {
    PyErr_Format(PyExc_RuntimeError, "game %s is not a valid value (holdem, holdem8, omaha, omaha8, 7stud, 7stud8, 7studnsq, razz, 5draw, 5draw8, 5drawnsq, lowball, lowball27)", game);
  }

  if (!PyList_Check(pypockets))
  {
    PyErr_SetString(PyExc_TypeError, "pockets must be list");
    goto err;
  }

  pockets_size = (int)PyList_Size(pypockets);

  {
    for (i = 0; i < pockets_size; i++)
    {
      int count;
      CardMask_RESET(pockets[i]);
      PyObject *pypocket = PyList_GetItem(pypockets, i);
      if (PyErr_Occurred())
        goto err;

      count = PyList2CardMask(pypocket, &pockets[i]);
      if (count < 0)
        goto err;
      if (count < (int)PyList_Size(pypocket))
        numToDeal[i + 1] = (int)PyList_Size(pypocket) - count;
      else
        numToDeal[i + 1] = 0;
    }
  }

  {
    int count;
    count = PyList2CardMask(pyboard, &board_cards);
    if (count < 0)
      goto err;
    if (count < (int)PyList_Size(pyboard))
      numToDeal[0] = (int)PyList_Size(pyboard) - count;
    else
      numToDeal[0] = 0;
  }

  if (pydead)
  {
    if (PyList2CardMask(pydead, &dead_cards) < 0)
      goto err;
  }
  else
  {
    CardMask_RESET(dead_cards);
  }

  {
    enum_result_t cresult;
    int err;
    memset(&cresult, '\0', sizeof(enum_result_t));

    if (iterations > 0)
    {
      err = pyenumSample(params->game, pockets, numToDeal, board_cards, dead_cards, pockets_size + 1, iterations, &cresult);
    }
    else
    {
      err = pyenumExhaustive(params->game, pockets, numToDeal, board_cards, dead_cards, pockets_size + 1, &cresult);
    }
    if (err != 0)
    {
      PyErr_Format(PyExc_RuntimeError, "poker-eval: pyenum returned error code %d", err);
      return 0;
    }

    result = PyList_New(0);

    PyObject *tmp;
    tmp = Py_BuildValue("(iii)", cresult.nsamples, params->haslopot, params->hashipot);
    PyList_Append(result, tmp);
    Py_DECREF(tmp);
    for (i = 0; i < pockets_size; i++)
    {
      tmp = Py_BuildValue("(iiiiiiid)",
                          cresult.nscoop[i],
                          cresult.nwinhi[i],
                          cresult.nlosehi[i],
                          cresult.ntiehi[i],
                          cresult.nwinlo[i],
                          cresult.nloselo[i],
                          cresult.ntielo[i],
                          cresult.ev[i] / cresult.nsamples);
      PyList_Append(result, tmp);
      Py_DECREF(tmp);
    }
  }

err:
  return result;
}

// Implementation of py_omaha_hand_instantiate
static PyObject *py_omaha_hand_instantiate(PyObject *self, PyObject *args)
{
  char *omaha_hand_text = NULL;
  PyObject *py_dead_cards_list = NULL; // Optional

  if (!PyArg_ParseTuple(args, "s|O", &omaha_hand_text, &py_dead_cards_list))
  {
    return NULL; // PyArg_ParseTuple sets the error
  }

  OmahaHandQuery query;
  if (!OmahaHand_Parse(omaha_hand_text, &query))
  {
    PyErr_Format(PyExc_RuntimeError, "Invalid Omaha hand string format: %s", omaha_hand_text);
    return NULL;
  }

  StdDeck_CardMask dead_mask;
  StdDeck_CardMask_RESET(dead_mask);
  if (py_dead_cards_list && py_dead_cards_list != Py_None)
  {
    if (!PyList_Check(py_dead_cards_list))
    { // Added check for list type
      PyErr_SetString(PyExc_TypeError, "Dead cards parameter must be a list.");
      return NULL;
    }
    if (PyList2CardMask(py_dead_cards_list, &dead_mask) < 0)
    {
      // PyList2CardMask should have already set an error
      return NULL;
    }
  }

  OmahaHandList hand_list;
  hand_list.count = 0;
  int num_combos = OmahaHand_Instantiate(&query, dead_mask, &hand_list);

  if (num_combos < 0)
  {
    PyErr_SetString(PyExc_RuntimeError, "Omaha hand instantiation failed (e.g., MAX_OMAHA_COMBOS limit reached or internal error).");
    return NULL;
  }

  PyObject *py_results_list = PyList_New(num_combos);
  if (!py_results_list)
  {
    // PyList_New can fail if num_combos is too large or memory allocation fails
    return PyErr_NoMemory();
  }

  for (int i = 0; i < num_combos; ++i)
  {
    StdDeck_CardMask omaha_mask = hand_list.hands[i];
    PyObject *py_single_hand_list = PyList_New(4); // Omaha hands always have 4 cards

    if (!py_single_hand_list)
    {
      Py_DECREF(py_results_list);
      return PyErr_NoMemory();
    }

    char card_str_buffer[4];
    int cards_in_hand_count = 0;

    for (int card_idx = 0; card_idx < StdDeck_N_CARDS && cards_in_hand_count < 4; ++card_idx)
    {
      if (StdDeck_CardMask_CARD_IS_SET(omaha_mask, card_idx))
      {
        StdDeck.cardToString(card_idx, card_str_buffer);
        PyObject *py_card_str = PyUnicode_FromString(card_str_buffer);

        if (!py_card_str)
        {
          Py_DECREF(py_single_hand_list);
          Py_DECREF(py_results_list);
          return PyErr_NoMemory();
        }

        if (PyList_SetItem(py_single_hand_list, cards_in_hand_count, py_card_str) < 0)
        {
          // PyList_SetItem DECREFs py_card_str on error.
          Py_DECREF(py_single_hand_list);
          Py_DECREF(py_results_list);
          return NULL; // Error already set by PyList_SetItem
        }
        // PyList_SetItem steals the reference to py_card_str, so no Py_DECREF(py_card_str) needed on success.
        cards_in_hand_count++;
      }
    }

    if (cards_in_hand_count != 4)
    {
      PyErr_Format(PyExc_RuntimeError, "Internal error: Generated Omaha hand %d does not have 4 cards (found %d).", i, cards_in_hand_count);
      Py_DECREF(py_single_hand_list);
      Py_DECREF(py_results_list);
      return NULL;
    }

    if (PyList_SetItem(py_results_list, i, py_single_hand_list) < 0)
    {
      // PyList_SetItem DECREFs py_single_hand_list on error.
      Py_DECREF(py_results_list);
      return NULL; // Error already set by PyList_SetItem
    }
    // PyList_SetItem steals the reference to py_single_hand_list, so no Py_DECREF(py_single_hand_list) needed on success.
  }

  return py_results_list;
}

// Implementation of py_stud_hand_instantiate
static PyObject *py_stud_hand_instantiate(PyObject *self, PyObject *args)
{
  char *stud_hand_text = NULL;
  int game_total_cards = 0;
  PyObject *py_dead_cards_list = NULL; // Optional

  if (!PyArg_ParseTuple(args, "si|O", &stud_hand_text, &game_total_cards, &py_dead_cards_list))
  {
    return NULL; // PyArg_ParseTuple sets the error
  }

  if (game_total_cards <= 0 || game_total_cards > MAX_STUD_CARDS)
  {
    PyErr_Format(PyExc_ValueError, "game_total_cards must be between 1 and %d.", MAX_STUD_CARDS);
    return NULL;
  }

  StudHandQuery query;
  if (!StudHand_Parse(stud_hand_text, game_total_cards, &query))
  {
    PyErr_Format(PyExc_RuntimeError, "Invalid Stud hand string format or parameters for hand: %s", stud_hand_text);
    return NULL;
  }

  StdDeck_CardMask dead_mask;
  StdDeck_CardMask_RESET(dead_mask);
  if (py_dead_cards_list && py_dead_cards_list != Py_None)
  {
    if (!PyList_Check(py_dead_cards_list))
    {
      PyErr_SetString(PyExc_TypeError, "Dead cards parameter must be a list.");
      return NULL;
    }
    if (PyList2CardMask(py_dead_cards_list, &dead_mask) < 0)
    {
      // PyList2CardMask should have already set an error
      return NULL;
    }
  }

  StudHandList hand_list;
  hand_list.count = 0;
  int num_combos = StudHand_Instantiate(&query, dead_mask, &hand_list);

  if (num_combos < 0)
  {
    // StudHand_Instantiate might return -1 for specific errors like MAX_STUD_COMBOS,
    // or 0 if query cannot be fulfilled (e.g. conflict).
    // The prompt suggests -1 for "failed or limit reached".
    PyErr_SetString(PyExc_RuntimeError, "Stud hand instantiation failed (e.g., MAX_STUD_COMBOS limit reached or internal error).");
    return NULL;
  }

  PyObject *py_results_list = PyList_New(num_combos);
  if (!py_results_list)
  {
    return PyErr_NoMemory();
  }

  for (int i = 0; i < num_combos; ++i)
  {
    StdDeck_CardMask stud_mask = hand_list.hands[i];

    int actual_cards_in_hand = 0;
    for (int c = 0; c < StdDeck_N_CARDS; ++c)
    {
      if (StdDeck_CardMask_CARD_IS_SET(stud_mask, c))
      {
        actual_cards_in_hand++;
      }
    }
    // This should ideally be equal to game_total_cards
    if (actual_cards_in_hand != game_total_cards && num_combos > 0)
    {
      PyErr_Format(PyExc_RuntimeError, "Internal error: Generated Stud hand %d has %d cards, expected %d.", i, actual_cards_in_hand, game_total_cards);
      Py_DECREF(py_results_list);
      return NULL;
    }

    PyObject *py_single_hand_list = PyList_New(actual_cards_in_hand);
    if (!py_single_hand_list)
    {
      Py_DECREF(py_results_list);
      return PyErr_NoMemory();
    }

    char card_str_buffer[4];
    int list_idx = 0;
    for (int card_idx = 0; card_idx < StdDeck_N_CARDS && list_idx < actual_cards_in_hand; ++card_idx)
    {
      if (StdDeck_CardMask_CARD_IS_SET(stud_mask, card_idx))
      {
        StdDeck.cardToString(card_idx, card_str_buffer);
        PyObject *py_card_str = PyUnicode_FromString(card_str_buffer);

        if (!py_card_str)
        {
          Py_DECREF(py_single_hand_list);
          Py_DECREF(py_results_list);
          return PyErr_NoMemory();
        }

        // PyList_SetItem steals a reference to py_card_str
        if (PyList_SetItem(py_single_hand_list, list_idx++, py_card_str) < 0)
        {
          // PyList_SetItem DECREFs py_card_str on error.
          Py_DECREF(py_single_hand_list);
          Py_DECREF(py_results_list);
          return NULL;
        }
      }
    }

    // PyList_SetItem steals a reference to py_single_hand_list
    if (PyList_SetItem(py_results_list, i, py_single_hand_list) < 0)
    {
      // PyList_SetItem DECREFs py_single_hand_list on error.
      Py_DECREF(py_results_list);
      return NULL;
    }
  }
  return py_results_list;
}

// Python-callable C helper function
// Converts a Python list of card strings to a Python Long representing the uint64_t card mask value.
static PyObject *py_convert_card_strings_to_mask_value(PyObject *self, PyObject *args)
{
  PyObject *py_card_list = NULL;
  if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &py_card_list))
  {
    // PyArg_ParseTuple sets the error (e.g., TypeError if not a list)
    return NULL;
  }

  StdDeck_CardMask mask_struct;
  // PyList2CardMask converts list of card strings (or ints) to StdDeck_CardMask.
  // It sets Python error if conversion fails (e.g., invalid card string).
  if (PyList2CardMask(py_card_list, &mask_struct) < 0)
  {
    return NULL; // Error already set by PyList2CardMask
  }

  // Get the 64-bit card mask value from StdDeck_CardMask
  // StdDeck_CardMask has a uint64_t cards_n field when USE_INT64 is defined
  uint64_t mask_val = mask_struct.cards_n;

  return PyLong_FromUnsignedLongLong(mask_val);
}

// Helper to convert a Python list of card strings to a uint64_t mask value.
// Note: This helper is as per prompt; however, py_calculate_equity_for_ranges expects
// a list of uint64_t values already, implying this conversion happens Python-side.
// This helper could be used by Python if it were exposed, or by other C functions.
static bool py_list_to_cardmask_val(PyObject *py_card_list, uint64_t *p_mask_val)
{
  if (!py_card_list || !PyList_Check(py_card_list))
  {
    PyErr_SetString(PyExc_TypeError, "Expected a list of card strings for py_list_to_cardmask_val.");
    return false;
  }

  StdDeck_CardMask temp_mask;
  if (PyList2CardMask(py_card_list, &temp_mask) < 0)
  {
    // PyList2CardMask sets the Python error
    return false;
  }

  // Get the 64-bit card mask value from StdDeck_CardMask
  // StdDeck_CardMask has a uint64_t cards_n field when USE_INT64 is defined
  *p_mask_val = temp_mask.cards_n;
  return true;
}

static PyObject *py_calculate_equity_for_ranges(PyObject *self, PyObject *args)
{
  char *game_name_str;
  PyObject *py_list_of_player_ranges;
  PyObject *py_board_list;
  PyObject *py_dead_cards_list = NULL;
  int nboard_cards_to_deal;
  int use_montecarlo_int;
  int iterations_if_montecarlo;
  int orderflag = 0; // Default for optional arg
  int total_matchups;
  PyObject *info_tuple = NULL;

  PlayerRange *c_player_ranges = NULL;
  StdDeck_CardMask **c_player_hand_masks_data = NULL; // Array of (StdDeck_CardMask*)
  int num_players = 0;
  PyObject *py_return_list = NULL;
  enum_result_t aggregated_results;
  bool agg_results_allocated = false;

  if (!PyArg_ParseTuple(args, "sOOOiib|i",
                        &game_name_str,
                        &py_list_of_player_ranges,
                        &py_board_list,
                        &nboard_cards_to_deal,
                        &use_montecarlo_int,
                        &iterations_if_montecarlo,
                        &py_dead_cards_list, // This is optional, Py_None if not passed
                        &orderflag))
  {
    return NULL; // Error already set by PyArg_ParseTuple
  }

  enum_game_t game;
  enum_gameparams_t *params = NULL;

  // Convert game name to enum
  if (!strcmp(game_name_str, "holdem"))
  {
    game = game_holdem;
  }
  else if (!strcmp(game_name_str, "holdem8"))
  {
    game = game_holdem8;
  }
  else if (!strcmp(game_name_str, "omaha"))
  {
    game = game_omaha;
  }
  else if (!strcmp(game_name_str, "omaha8"))
  {
    game = game_omaha8;
  }
  else if (!strcmp(game_name_str, "7stud"))
  {
    game = game_7stud;
  }
  else if (!strcmp(game_name_str, "7stud8"))
  {
    game = game_7stud8;
  }
  else if (!strcmp(game_name_str, "7studnsq"))
  {
    game = game_7studnsq;
  }
  else if (!strcmp(game_name_str, "razz"))
  {
    game = game_razz;
  }
  else if (!strcmp(game_name_str, "5draw"))
  {
    game = game_5draw;
  }
  else if (!strcmp(game_name_str, "5draw8"))
  {
    game = game_5draw8;
  }
  else if (!strcmp(game_name_str, "5drawnsq"))
  {
    game = game_5drawnsq;
  }
  else if (!strcmp(game_name_str, "lowball"))
  {
    game = game_lowball;
  }
  else if (!strcmp(game_name_str, "lowball27"))
  {
    game = game_lowball27;
  }
  else
  {
    PyErr_Format(PyExc_ValueError, "Invalid game name: %s", game_name_str);
    return NULL;
  }

  params = enumGameParams(game);
  if (params == NULL)
  {
    PyErr_Format(PyExc_ValueError, "Failed to get game parameters for: %s", game_name_str);
    return NULL;
  }
  bool use_montecarlo = (use_montecarlo_int != 0);

  if (!PyList_Check(py_list_of_player_ranges))
  {
    PyErr_SetString(PyExc_TypeError, "player_ranges must be a list.");
    return NULL;
  }
  num_players = (int)PyList_Size(py_list_of_player_ranges);
  if (num_players <= 0 || num_players > ENUM_MAXPLAYERS)
  {
    PyErr_Format(PyExc_ValueError, "Number of players must be between 1 and %d.", ENUM_MAXPLAYERS);
    return NULL;
  }

  c_player_ranges = (PlayerRange *)malloc(num_players * sizeof(PlayerRange));
  c_player_hand_masks_data = (StdDeck_CardMask **)malloc(num_players * sizeof(StdDeck_CardMask *));
  double **c_player_weight_buffers = (double **)malloc(num_players * sizeof(double *));
  if (!c_player_ranges || !c_player_hand_masks_data || !c_player_weight_buffers)
  {
    PyErr_NoMemory();
    goto cleanup;
  }
  memset(c_player_ranges, 0, num_players * sizeof(PlayerRange));
  // Initialize pointers in c_player_hand_masks_data to NULL for safe cleanup
  for (int p = 0; p < num_players; ++p)
  {
    c_player_hand_masks_data[p] = NULL;
    c_player_weight_buffers[p] = NULL;
  }

  for (int p = 0; p < num_players; ++p)
  {
    PyObject *py_one_player_range = PyList_GetItem(py_list_of_player_ranges, p);
    if (!py_one_player_range || !PyList_Check(py_one_player_range))
    {
      PyErr_Format(PyExc_TypeError, "Player range at index %d must be a list.", p);
      goto cleanup;
    }
    c_player_ranges[p].count = (int)PyList_Size(py_one_player_range);
    if (c_player_ranges[p].count == 0)
    {
      PyErr_Format(PyExc_ValueError, "Player range at index %d cannot be empty.", p);
      goto cleanup;
    }
    c_player_ranges[p].weights = NULL;
    c_player_ranges[p].total_weight = (double)c_player_ranges[p].count;

    c_player_hand_masks_data[p] = (StdDeck_CardMask *)malloc(c_player_ranges[p].count * sizeof(StdDeck_CardMask));
    if (!c_player_hand_masks_data[p])
    {
      PyErr_NoMemory();
      goto cleanup;
    }
    c_player_ranges[p].hand_masks = c_player_hand_masks_data[p];

    double *weights_buffer = NULL;
    for (int h = 0; h < c_player_ranges[p].count; ++h)
    {
      PyObject *py_entry = PyList_GetItem(py_one_player_range, h);
      if (!py_entry)
      {
        PyErr_Format(PyExc_TypeError, "Hand entry at player %d, index %d is null.", p, h);
        goto cleanup;
      }

      PyObject *py_mask_obj = py_entry;
      double weight_value = 1.0;

      if (PyTuple_Check(py_entry))
      {
        Py_ssize_t tuple_size = PyTuple_Size(py_entry);
        if (tuple_size < 1)
        {
          PyErr_Format(PyExc_ValueError, "Tuple entry at player %d, hand %d must contain at least a mask.", p, h);
          goto cleanup;
        }
        py_mask_obj = PyTuple_GetItem(py_entry, 0);
        if (tuple_size > 1)
        {
          PyObject *py_weight_obj = PyTuple_GetItem(py_entry, 1);
          weight_value = PyFloat_AsDouble(py_weight_obj);
          if (PyErr_Occurred())
          {
            PyErr_Format(PyExc_TypeError, "Weight at player %d, hand %d must be convertible to float.", p, h);
            goto cleanup;
          }
          if (weight_value < 0.0)
          {
            PyErr_Format(PyExc_ValueError, "Weight at player %d, hand %d cannot be negative.", p, h);
            goto cleanup;
          }
        }
        if (tuple_size > 2)
        {
          PyErr_Format(PyExc_ValueError, "Tuple entry at player %d, hand %d must have at most 2 elements (mask, weight).", p, h);
          goto cleanup;
        }
      }

      if (!PyLong_Check(py_mask_obj))
      {
        PyErr_Format(PyExc_TypeError, "Hand mask at player %d, hand %d must be an integer or tuple(mask, weight).", p, h);
        goto cleanup;
      }

      unsigned PY_LONG_LONG ull_mask = PyLong_AsUnsignedLongLong(py_mask_obj);
      if (PyErr_Occurred())
      { // Error during conversion (e.g., overflow for non-unsigned long long)
        goto cleanup;
      }
      // Populate StdDeck_CardMask (uint32_t cards_n[2]) from uint64_t
      c_player_hand_masks_data[p][h].cards_n = ull_mask;

      if ((fabs(weight_value - 1.0) > 1e-9) || PyTuple_Check(py_entry))
      {
        if (!weights_buffer)
        {
          weights_buffer = (double *)malloc(c_player_ranges[p].count * sizeof(double));
          if (!weights_buffer)
          {
            PyErr_NoMemory();
            goto cleanup;
          }
          for (int fill = 0; fill < h; ++fill)
          {
            weights_buffer[fill] = 1.0;
          }
          c_player_ranges[p].weights = weights_buffer;
          c_player_weight_buffers[p] = weights_buffer;
        }
        weights_buffer[h] = weight_value;
      }
      else if (weights_buffer)
      {
        weights_buffer[h] = 1.0;
      }
    }

    if (weights_buffer)
    {
      double total_weight = 0.0;
      for (int h = 0; h < c_player_ranges[p].count; ++h)
      {
        total_weight += weights_buffer[h];
      }
      c_player_ranges[p].total_weight = total_weight;
    }
    else
    {
      c_player_ranges[p].total_weight = (double)c_player_ranges[p].count;
    }
  }

  StdDeck_CardMask board_mask;
  StdDeck_CardMask_RESET(board_mask);
  if (!PyList_Check(py_board_list))
  {
    PyErr_SetString(PyExc_TypeError, "Board cards parameter must be a list.");
    goto cleanup;
  }
  if (PyList2CardMask(py_board_list, &board_mask) < 0)
    goto cleanup;

  StdDeck_CardMask dead_mask_initial;
  StdDeck_CardMask_RESET(dead_mask_initial);
  if (py_dead_cards_list && py_dead_cards_list != Py_None)
  {
    if (!PyList_Check(py_dead_cards_list))
    {
      PyErr_SetString(PyExc_TypeError, "Dead cards parameter must be a list.");
      goto cleanup;
    }
    if (PyList2CardMask(py_dead_cards_list, &dead_mask_initial) < 0)
      goto cleanup;
  }

  if (enumResultAlloc(&aggregated_results, num_players, enum_ordering_mode_none) != 0)
  {
    PyErr_SetString(PyExc_MemoryError, "Failed to allocate enum_result_t for aggregated results.");
    goto cleanup;
  }
  agg_results_allocated = true;

  total_matchups = CalculateEquityForRanges(game, c_player_ranges, num_players, board_mask,
                                                dead_mask_initial, nboard_cards_to_deal,
                                                use_montecarlo, iterations_if_montecarlo,
                                                orderflag, &aggregated_results);

  if (total_matchups < 0)
  { // Indicates a critical error from CalculateEquityForRanges
    PyErr_SetString(PyExc_RuntimeError, "CalculateEquityForRanges C function failed.");
    goto cleanup;
  }

  // Convert aggregated_results to Python list format [info_tuple, player1_results_tuple, ...]
  py_return_list = PyList_New(0);
  if (!py_return_list)
    goto cleanup;

  info_tuple = Py_BuildValue("(iii)", aggregated_results.nsamples, params->haslopot, params->hashipot);
  if (!info_tuple || PyList_Append(py_return_list, info_tuple) < 0)
  {
    Py_XDECREF(info_tuple);
    goto cleanup;
  }
  Py_DECREF(info_tuple);

  for (int p = 0; p < num_players; ++p)
  {
    PyObject *player_results_tuple = Py_BuildValue("(iiiiiiid)",
                                                   aggregated_results.nscoop[p],
                                                   aggregated_results.nwinhi[p],
                                                   aggregated_results.nlosehi[p],
                                                   aggregated_results.ntiehi[p],
                                                   aggregated_results.nwinlo[p],
                                                   aggregated_results.nloselo[p],
                                                   aggregated_results.ntielo[p],
                                                   aggregated_results.ev[p]); // EV is already normalized
    if (!player_results_tuple || PyList_Append(py_return_list, player_results_tuple) < 0)
    {
      Py_XDECREF(player_results_tuple);
      goto cleanup;
    }
    Py_DECREF(player_results_tuple);
  }

cleanup:
  if (agg_results_allocated)
  {
    enumResultFree(&aggregated_results);
  }
  if (c_player_hand_masks_data)
  {
    for (int p = 0; p < num_players; ++p)
    {
      if (c_player_hand_masks_data[p])
      {
        free(c_player_hand_masks_data[p]);
      }
    }
    free(c_player_hand_masks_data);
  }
  if (c_player_ranges)
  {
    for (int p = 0; p < num_players; ++p)
    {
      if (c_player_weight_buffers && c_player_weight_buffers[p])
      {
        free(c_player_weight_buffers[p]);
        c_player_weight_buffers[p] = NULL;
      }
    }
    free(c_player_ranges);
  }
  if (c_player_weight_buffers)
  {
    free(c_player_weight_buffers);
  }

  if (PyErr_Occurred() && py_return_list)
  {
    Py_DECREF(py_return_list); // If an error occurred after list was created but before success
    py_return_list = NULL;
  }
  return py_return_list; // Will be NULL if an error occurred
}

static PyObject *calculate_multiway_equity(PyObject *self, PyObject *args, PyObject *kwargs)
{
  static const char *kwlist[] = {"game", "player_ranges", "invested", "board", "dead_cards", "num_board_cards_to_deal", "use_montecarlo", "iterations", "orderflag", NULL};

  const char *game_name_str;
  PyObject *py_list_of_player_ranges;
  PyObject *py_invested_list;
  PyObject *py_board_list = Py_None;
  PyObject *py_dead_cards_list = Py_None;
  int nboard_cards_to_deal = 0;
  int use_montecarlo_int = 0;
  int iterations_if_montecarlo = 0;
  int orderflag = 0;
  int total_matchups;

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sOO|OOiii", (char **)(void *)kwlist,
                                   &game_name_str,
                                   &py_list_of_player_ranges,
                                   &py_invested_list,
                                   &py_board_list,
                                   &py_dead_cards_list,
                                   &nboard_cards_to_deal,
                                   &use_montecarlo_int,
                                   &iterations_if_montecarlo,
                                   &orderflag))
  {
    return NULL;
  }

  if (!PyList_Check(py_list_of_player_ranges))
  {
    PyErr_SetString(PyExc_TypeError, "player_ranges must be a list");
    return NULL;
  }

  if (!PyList_Check(py_invested_list))
  {
    PyErr_SetString(PyExc_TypeError, "invested must be a list");
    return NULL;
  }

  PyObject *owned_board_list = NULL;
  if (py_board_list == NULL || py_board_list == Py_None)
  {
    owned_board_list = PyList_New(0);
    if (!owned_board_list)
      return NULL;
    py_board_list = owned_board_list;
  }

  bool use_montecarlo = (use_montecarlo_int != 0);

  enum_game_t game;
  enum_gameparams_t *params = NULL;
  if (!strcmp(game_name_str, "holdem"))
    game = game_holdem;
  else if (!strcmp(game_name_str, "holdem8"))
    game = game_holdem8;
  else if (!strcmp(game_name_str, "omaha"))
    game = game_omaha;
  else if (!strcmp(game_name_str, "omaha8"))
    game = game_omaha8;
  else if (!strcmp(game_name_str, "7stud"))
    game = game_7stud;
  else if (!strcmp(game_name_str, "7stud8"))
    game = game_7stud8;
  else if (!strcmp(game_name_str, "7studnsq"))
    game = game_7studnsq;
  else if (!strcmp(game_name_str, "razz"))
    game = game_razz;
  else if (!strcmp(game_name_str, "5draw"))
    game = game_5draw;
  else if (!strcmp(game_name_str, "5draw8"))
    game = game_5draw8;
  else if (!strcmp(game_name_str, "5drawnsq"))
    game = game_5drawnsq;
  else if (!strcmp(game_name_str, "lowball"))
    game = game_lowball;
  else if (!strcmp(game_name_str, "lowball27"))
    game = game_lowball27;
  else
  {
    if (owned_board_list)
      Py_DECREF(owned_board_list);
    PyErr_Format(PyExc_ValueError, "Invalid game name: %s", game_name_str);
    return NULL;
  }

  params = enumGameParams(game);
  if (!params)
  {
    if (owned_board_list)
      Py_DECREF(owned_board_list);
    PyErr_Format(PyExc_ValueError, "Failed to get game parameters for: %s", game_name_str);
    return NULL;
  }

  int num_players = (int)PyList_Size(py_list_of_player_ranges);
  if (num_players <= 0 || num_players > ENUM_MAXPLAYERS)
  {
    if (owned_board_list)
      Py_DECREF(owned_board_list);
    PyErr_Format(PyExc_ValueError, "Number of players must be between 1 and %d.", ENUM_MAXPLAYERS);
    return NULL;
  }

  if (PyList_Size(py_invested_list) != num_players)
  {
    if (owned_board_list)
      Py_DECREF(owned_board_list);
    PyErr_SetString(PyExc_ValueError, "Length of invested list must match number of players");
    return NULL;
  }

  PlayerRange *c_player_ranges = NULL;
  StdDeck_CardMask **c_player_hand_masks_data = NULL;
  double **c_player_weight_buffers = NULL;
  double *c_invested = NULL;
  double *c_stack_sizes = NULL;
  PyObject *py_return_dict = NULL;
  PyObject *py_matchups = NULL;
  PyObject *py_samples = NULL;
  PyObject *py_ev_list = NULL;
  PyObject *py_equity_list = NULL;
  PyObject *py_win_list = NULL;
  PyObject *py_tie_list = NULL;

  c_player_ranges = (PlayerRange *)malloc(num_players * sizeof(PlayerRange));
  c_player_hand_masks_data = (StdDeck_CardMask **)malloc(num_players * sizeof(StdDeck_CardMask *));
  c_player_weight_buffers = (double **)malloc(num_players * sizeof(double *));
  c_invested = (double *)malloc(num_players * sizeof(double));
  c_stack_sizes = (double *)malloc(num_players * sizeof(double));
  if (!c_player_ranges || !c_player_hand_masks_data || !c_player_weight_buffers || !c_invested || !c_stack_sizes)
  {
    PyErr_NoMemory();
    goto cleanup_multi;
  }

  memset(c_player_ranges, 0, num_players * sizeof(PlayerRange));
  for (int i = 0; i < num_players; ++i)
  {
    c_player_hand_masks_data[i] = NULL;
    c_player_weight_buffers[i] = NULL;
    c_stack_sizes[i] = 0.0;
  }

  for (int p = 0; p < num_players; ++p)
  {
    PyObject *py_one_player_range = PyList_GetItem(py_list_of_player_ranges, p);
    if (!py_one_player_range || !PyList_Check(py_one_player_range))
    {
      PyErr_Format(PyExc_TypeError, "Player range at index %d must be a list.", p);
      goto cleanup_multi;
    }
    c_player_ranges[p].count = (int)PyList_Size(py_one_player_range);
    if (c_player_ranges[p].count <= 0)
    {
      PyErr_Format(PyExc_ValueError, "Player range at index %d cannot be empty.", p);
      goto cleanup_multi;
    }

    c_player_ranges[p].weights = NULL;
    c_player_ranges[p].total_weight = (double)c_player_ranges[p].count;

    c_player_hand_masks_data[p] = (StdDeck_CardMask *)malloc((size_t)c_player_ranges[p].count * sizeof(StdDeck_CardMask));
    if (!c_player_hand_masks_data[p])
    {
      PyErr_NoMemory();
      goto cleanup_multi;
    }
    c_player_ranges[p].hand_masks = c_player_hand_masks_data[p];

    double *weights_buffer = NULL;
    for (int h = 0; h < c_player_ranges[p].count; ++h)
    {
      PyObject *py_entry = PyList_GetItem(py_one_player_range, h);
      if (!py_entry)
      {
        PyErr_Format(PyExc_TypeError, "Hand entry at player %d, index %d is null.", p, h);
        goto cleanup_multi;
      }

      PyObject *py_mask_obj = py_entry;
      double weight_value = 1.0;

      if (PyTuple_Check(py_entry))
      {
        Py_ssize_t tuple_size = PyTuple_Size(py_entry);
        if (tuple_size < 1)
        {
          PyErr_Format(PyExc_ValueError, "Tuple entry at player %d, hand %d must contain at least a mask.", p, h);
          goto cleanup_multi;
        }
        py_mask_obj = PyTuple_GetItem(py_entry, 0);
        if (tuple_size > 1)
        {
          PyObject *py_weight_obj = PyTuple_GetItem(py_entry, 1);
          weight_value = PyFloat_AsDouble(py_weight_obj);
          if (PyErr_Occurred())
          {
            PyErr_Format(PyExc_TypeError, "Weight at player %d, hand %d must be convertible to float.", p, h);
            goto cleanup_multi;
          }
          if (weight_value < 0.0)
          {
            PyErr_Format(PyExc_ValueError, "Weight at player %d, hand %d cannot be negative.", p, h);
            goto cleanup_multi;
          }
        }
        if (tuple_size > 2)
        {
          PyErr_Format(PyExc_ValueError, "Tuple entry at player %d, hand %d must have at most 2 elements (mask, weight).", p, h);
          goto cleanup_multi;
        }
      }

      if (!PyLong_Check(py_mask_obj))
      {
        PyErr_Format(PyExc_TypeError, "Hand mask at player %d, hand %d must be an integer or tuple(mask, weight).", p, h);
        goto cleanup_multi;
      }

      unsigned PY_LONG_LONG ull_mask = PyLong_AsUnsignedLongLong(py_mask_obj);
      if (PyErr_Occurred())
      {
        goto cleanup_multi;
      }

      c_player_hand_masks_data[p][h].cards_n = ull_mask;

      if ((fabs(weight_value - 1.0) > 1e-9) || PyTuple_Check(py_entry))
      {
        if (!weights_buffer)
        {
          weights_buffer = (double *)malloc((size_t)c_player_ranges[p].count * sizeof(double));
          if (!weights_buffer)
          {
            PyErr_NoMemory();
            goto cleanup_multi;
          }
          for (int fill = 0; fill < h; ++fill)
          {
            weights_buffer[fill] = 1.0;
          }
          c_player_ranges[p].weights = weights_buffer;
          c_player_weight_buffers[p] = weights_buffer;
        }
        weights_buffer[h] = weight_value;
      }
      else if (weights_buffer)
      {
        weights_buffer[h] = 1.0;
      }
    }

    if (weights_buffer)
    {
      double total_weight = 0.0;
      for (int h = 0; h < c_player_ranges[p].count; ++h)
      {
        total_weight += weights_buffer[h];
      }
      c_player_ranges[p].total_weight = total_weight;
    }
    else
    {
      c_player_ranges[p].total_weight = (double)c_player_ranges[p].count;
    }
  }

  for (int p = 0; p < num_players; ++p)
  {
    PyObject *py_inv = PyList_GetItem(py_invested_list, p);
    double value = PyFloat_AsDouble(py_inv);
    if (PyErr_Occurred())
    {
      PyErr_Format(PyExc_TypeError, "Invested amount at index %d must be numeric.", p);
      goto cleanup_multi;
    }
    c_invested[p] = value;
  }

  StdDeck_CardMask board_mask;
  StdDeck_CardMask_RESET(board_mask);
  if (!PyList_Check(py_board_list))
  {
    PyErr_SetString(PyExc_TypeError, "Board cards parameter must be a list.");
    goto cleanup_multi;
  }
  if (PyList2CardMask(py_board_list, &board_mask) < 0)
    goto cleanup_multi;

  StdDeck_CardMask dead_mask_initial;
  StdDeck_CardMask_RESET(dead_mask_initial);
  if (py_dead_cards_list && py_dead_cards_list != Py_None)
  {
    if (!PyList_Check(py_dead_cards_list))
    {
      PyErr_SetString(PyExc_TypeError, "Dead cards parameter must be a list.");
      goto cleanup_multi;
    }
    if (PyList2CardMask(py_dead_cards_list, &dead_mask_initial) < 0)
      goto cleanup_multi;
  }

  MultiwayPotState state;
  state.ranges = c_player_ranges;
  state.stack_sizes = c_stack_sizes;
  state.invested = c_invested;
  state.num_players = num_players;

  MultiwayEquityOptions options;
  options.use_montecarlo = use_montecarlo;
  options.iterations = iterations_if_montecarlo;
  options.orderflag = orderflag;

  MultiwayEquityResult result;
  memset(&result, 0, sizeof(result));

  total_matchups = CalculateMultiwayEquity(game, &state, board_mask, dead_mask_initial,
                                               nboard_cards_to_deal, &options, &result);

  if (total_matchups < 0)
  {
    PyErr_SetString(PyExc_RuntimeError, "CalculateMultiwayEquity C function failed.");
    goto cleanup_multi;
  }

  py_return_dict = PyDict_New();
  if (!py_return_dict)
    goto cleanup_multi;

  py_matchups = PyLong_FromLong(total_matchups);
  py_samples = PyFloat_FromDouble(result.total_weighted_samples);
  if (!py_matchups || !py_samples)
  {
    Py_XDECREF(py_matchups);
    Py_XDECREF(py_samples);
    Py_DECREF(py_return_dict);
    py_return_dict = NULL;
    goto cleanup_multi;
  }

  PyDict_SetItemString(py_return_dict, "matchups", py_matchups);
  PyDict_SetItemString(py_return_dict, "total_weighted_samples", py_samples);
  Py_DECREF(py_matchups);
  Py_DECREF(py_samples);

  py_ev_list = PyList_New(num_players);
  py_equity_list = PyList_New(num_players);
  py_win_list = PyList_New(num_players);
  py_tie_list = PyList_New(num_players);
  if (!py_ev_list || !py_equity_list || !py_win_list || !py_tie_list)
  {
    Py_XDECREF(py_ev_list);
    Py_XDECREF(py_equity_list);
    Py_XDECREF(py_win_list);
    Py_XDECREF(py_tie_list);
    Py_DECREF(py_return_dict);
    py_return_dict = NULL;
    goto cleanup_multi;
  }

  for (int p = 0; p < num_players; ++p)
  {
    PyList_SET_ITEM(py_ev_list, p, PyFloat_FromDouble(result.ev[p]));
    PyList_SET_ITEM(py_equity_list, p, PyFloat_FromDouble(result.equity[p]));
    PyList_SET_ITEM(py_win_list, p, PyFloat_FromDouble(result.win_prob[p]));
    PyList_SET_ITEM(py_tie_list, p, PyFloat_FromDouble(result.tie_prob[p]));
  }

  PyDict_SetItemString(py_return_dict, "ev", py_ev_list);
  PyDict_SetItemString(py_return_dict, "equity", py_equity_list);
  PyDict_SetItemString(py_return_dict, "win_prob", py_win_list);
  PyDict_SetItemString(py_return_dict, "tie_prob", py_tie_list);
  Py_DECREF(py_ev_list);
  Py_DECREF(py_equity_list);
  Py_DECREF(py_win_list);
  Py_DECREF(py_tie_list);

cleanup_multi:
  if (owned_board_list)
    Py_DECREF(owned_board_list);

  if (c_player_ranges)
  {
    for (int p = 0; p < num_players; ++p)
    {
      if (c_player_weight_buffers && c_player_weight_buffers[p])
        free(c_player_weight_buffers[p]);
      if (c_player_hand_masks_data && c_player_hand_masks_data[p])
        free(c_player_hand_masks_data[p]);
    }
    free(c_player_ranges);
  }
  if (c_player_hand_masks_data)
    free(c_player_hand_masks_data);
  if (c_player_weight_buffers)
    free(c_player_weight_buffers);
  if (c_invested)
    free(c_invested);
  if (c_stack_sizes)
    free(c_stack_sizes);

  return py_return_dict;
}

typedef struct py_solver_v3_context_t {
    pe_solver_t *solver;
    pe_vector_game_t game;
    PyObject *game_object;
    PyObject *states;
} py_solver_v3_context_t;

static PyObject *py_solver_v3_object(const void *state)
{
    PyObject *object;
    memcpy(&object, &state, sizeof(object));
    return object;
}

static PyObject *py_solver_v3_call(py_solver_v3_context_t *ctx,
                                   const char *name, PyObject *args)
{
    PyObject *fn = PyObject_GetAttrString(ctx->game_object, name);
    PyObject *result;
    if (!fn)
        return NULL;
    if (!PyCallable_Check(fn)) {
        PyErr_Format(PyExc_TypeError, "game.%s must be callable", name);
        Py_DECREF(fn);
        return NULL;
    }
    result = PyObject_CallObject(fn, args);
    Py_DECREF(fn);
    return result;
}

static PyObject *py_solver_v3_one_state_args(const void *state)
{
    return PyTuple_Pack(1, py_solver_v3_object(state));
}

static PyObject *py_solver_v3_reach_object(const pe_reach_vec_t *reach,
                                            uint8_t players)
{
    PyObject *outer;
    uint8_t player;

    if (!reach) {
        PyErr_SetString(PyExc_ValueError, "terminal_values received no reach vectors");
        return NULL;
    }
    outer = PyList_New((Py_ssize_t)players);
    if (!outer)
        return NULL;
    for (player = 0u; player < players; ++player) {
        PyObject *values;
        size_t combo;
        if (reach[player].n > (size_t)PY_SSIZE_T_MAX) {
            PyErr_SetString(PyExc_OverflowError,
                            "reach vector is too large for Python");
            Py_DECREF(outer);
            return NULL;
        }
        values = PyList_New((Py_ssize_t)reach[player].n);
        if (!values) {
            Py_DECREF(outer);
            return NULL;
        }
        for (combo = 0u; combo < reach[player].n; ++combo) {
            PyObject *value = PyFloat_FromDouble(reach[player].v[combo]);
            if (!value) {
                Py_DECREF(values);
                Py_DECREF(outer);
                return NULL;
            }
            PyList_SET_ITEM(values, (Py_ssize_t)combo, value);
        }
        PyList_SET_ITEM(outer, (Py_ssize_t)player, values);
    }
    return outer;
}

static const void *py_solver_v3_store_state(py_solver_v3_context_t *ctx,
                                             PyObject *state)
{
    if (PyList_Append(ctx->states, state) != 0)
        return NULL;
    return (const void *)state;
}

static void py_solver_v3_release_state(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    Py_ssize_t index;
    if (!ctx || !ctx->states || !state || state == ctx->game.root)
        return;
    /* The vector-game contract releases one ownership acquired by each
       apply_action call.  Remove one matching list entry, rather than
       retaining every visit until capsule destruction. */
    for (index = PyList_GET_SIZE(ctx->states) - 1; index >= 0; --index)
        if ((const void *)PyList_GET_ITEM(ctx->states, index) == state) {
            if (PySequence_DelItem(ctx->states, index) != 0)
                PyErr_Clear();
            return;
        }
}

static int py_solver_v3_is_terminal(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    int value;
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "is_terminal", args);
    Py_DECREF(args);
    if (!result)
        return -1;
    value = PyObject_IsTrue(result);
    Py_DECREF(result);
    return value;
}

static int py_solver_v3_acting_player(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    long value;
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "acting_player", args);
    Py_DECREF(args);
    if (!result)
        return -1;
    value = PyLong_AsLong(result);
    Py_DECREF(result);
    if (PyErr_Occurred() || value < 0 || value >= (long)ctx->game.player_count) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "acting_player is out of range");
        return -1;
    }
    return (int)value;
}

static uint16_t py_solver_v3_action_count(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    unsigned long value;
    if (!args)
        return 0u;
    result = py_solver_v3_call(ctx, "action_count", args);
    Py_DECREF(args);
    if (!result)
        return 0u;
    value = PyLong_AsUnsignedLong(result);
    Py_DECREF(result);
    if (PyErr_Occurred() || value == 0ul || value > 65535ul) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "action_count must be in 1..65535");
        return 0u;
    }
    return (uint16_t)value;
}

static uint64_t py_solver_v3_infoset_key(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    unsigned long long value;
    if (!args)
        return 0u;
    result = py_solver_v3_call(ctx, "infoset_key", args);
    Py_DECREF(args);
    if (!result)
        return 0u;
    value = PyLong_AsUnsignedLongLong(result);
    Py_DECREF(result);
    return PyErr_Occurred() ? 0u : (uint64_t)value;
}

static const void *py_solver_v3_apply_action(const void *state,
                                              uint16_t action, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *action_object = PyLong_FromUnsignedLong((unsigned long)action);
    PyObject *args;
    PyObject *result;
    const void *stored;
    if (!action_object)
        return NULL;
    args = PyTuple_Pack(2, py_solver_v3_object(state), action_object);
    Py_DECREF(action_object);
    if (!args)
        return NULL;
    result = py_solver_v3_call(ctx, "apply_action", args);
    Py_DECREF(args);
    if (!result)
        return NULL;
    if (result == Py_None) {
        PyErr_SetString(PyExc_ValueError, "apply_action returned None");
        Py_DECREF(result);
        return NULL;
    }
    stored = py_solver_v3_store_state(ctx, result);
    Py_DECREF(result);
    return stored;
}

static int py_solver_v3_combo_compatible(const void *state, uint8_t player,
                                         uint16_t player_combo,
                                         uint8_t opponent,
                                         uint16_t opponent_combo, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = Py_BuildValue("(OKKKK)", py_solver_v3_object(state),
                                   (unsigned long long)player,
                                   (unsigned long long)player_combo,
                                   (unsigned long long)opponent,
                                   (unsigned long long)opponent_combo);
    PyObject *result;
    int value;
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "combo_compatible", args);
    Py_DECREF(args);
    if (!result)
        return -1;
    value = PyObject_IsTrue(result);
    Py_DECREF(result);
    return value;
}

static int py_solver_v3_is_chance(const void *state, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    int value;
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "is_chance", args);
    Py_DECREF(args);
    if (!result)
        return -1;
    value = PyObject_IsTrue(result);
    Py_DECREF(result);
    return value;
}

static uint16_t py_solver_v3_chance_outcome_count(const void *state,
                                                   void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = py_solver_v3_one_state_args(state);
    PyObject *result;
    unsigned long value;
    if (!args)
        return 0u;
    result = py_solver_v3_call(ctx, "chance_outcome_count", args);
    Py_DECREF(args);
    if (!result)
        return 0u;
    value = PyLong_AsUnsignedLong(result);
    Py_DECREF(result);
    if (PyErr_Occurred() || value == 0ul || value > (unsigned long)UINT16_MAX) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError,
                            "chance_outcome_count must be in 1..65535");
        return 0u;
    }
    return (uint16_t)value;
}

static double py_solver_v3_chance_outcome_weight(const void *state,
                                                  uint16_t outcome,
                                                  void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = Py_BuildValue("(OK)", py_solver_v3_object(state),
                                   (unsigned long long)outcome);
    PyObject *result;
    double value;
    if (!args)
        return NAN;
    result = py_solver_v3_call(ctx, "chance_outcome_weight", args);
    Py_DECREF(args);
    if (!result)
        return NAN;
    value = PyFloat_AsDouble(result);
    Py_DECREF(result);
    return value;
}

static const void *py_solver_v3_apply_chance(const void *state, int outcome,
                                              void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = Py_BuildValue("(Oi)", py_solver_v3_object(state), outcome);
    PyObject *result;
    const void *stored;
    if (!args)
        return NULL;
    result = py_solver_v3_call(ctx, "apply_chance", args);
    Py_DECREF(args);
    if (!result)
        return NULL;
    if (result == Py_None) {
        PyErr_SetString(PyExc_ValueError, "apply_chance returned None");
        Py_DECREF(result);
        return NULL;
    }
    stored = py_solver_v3_store_state(ctx, result);
    Py_DECREF(result);
    return stored;
}

static int py_solver_v3_strategy_callback(const void *state, uint64_t key,
                                          uint16_t action, pe_value_vec_t *out,
                                          void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *args = Py_BuildValue("(OKKK)", py_solver_v3_object(state),
                                   (unsigned long long)key,
                                   (unsigned long long)action,
                                   (unsigned long long)out->n);
    PyObject *result;
    PyObject *sequence;
    Py_ssize_t i;
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "strategy", args);
    Py_DECREF(args);
    if (!result)
        return -1;
    sequence = PySequence_Fast(result, "game.strategy must return a sequence");
    Py_DECREF(result);
    if (!sequence)
        return -1;
    if (PySequence_Fast_GET_SIZE(sequence) != (Py_ssize_t)out->n) {
        PyErr_SetString(PyExc_ValueError,
                        "game.strategy returned the wrong combo count");
        Py_DECREF(sequence);
        return -1;
    }
    for (i = 0; i < (Py_ssize_t)out->n; ++i) {
        out->v[i] = PyFloat_AsDouble(PySequence_Fast_GET_ITEM(sequence, i));
        if (PyErr_Occurred()) {
            Py_DECREF(sequence);
            return -1;
        }
    }
    Py_DECREF(sequence);
    return 0;
}

static int py_solver_v3_terminal_values(const void *state,
                                        const pe_reach_vec_t *reach,
                                        pe_value_vec_t *out,
                                        uint8_t players, void *user)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)user;
    PyObject *reach_object = py_solver_v3_reach_object(reach, players);
    PyObject *args;
    PyObject *result;
    PyObject *outer;
    uint8_t player;
    if (!reach_object)
        return -1;
    args = PyTuple_Pack(2, py_solver_v3_object(state), reach_object);
    Py_DECREF(reach_object);
    if (!args)
        return -1;
    result = py_solver_v3_call(ctx, "terminal_values", args);
    Py_DECREF(args);
    /* Keep the original one-argument callback usable while allowing new
       callbacks to consume blocker/range reach. A TypeError is retried only
       when it looks like Python rejected the callback arity; TypeErrors raised
       by the callback body are preserved. */
    if (!result && PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyObject *type = NULL;
        PyObject *value = NULL;
        PyObject *traceback = NULL;
        PyObject *message = NULL;
        const char *text = NULL;
        int wrong_arity = 0;
        PyErr_Fetch(&type, &value, &traceback);
        message = PyObject_Str(value);
        if (message)
            text = PyUnicode_AsUTF8(message);
        if (text && strstr(text, "argument") != NULL &&
            (strstr(text, "given") != NULL ||
             strstr(text, "required") != NULL ||
             strstr(text, "exactly") != NULL))
            wrong_arity = 1;
        Py_XDECREF(message);
        if (wrong_arity) {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            args = py_solver_v3_one_state_args(state);
            if (!args)
                return -1;
            result = py_solver_v3_call(ctx, "terminal_values", args);
            Py_DECREF(args);
        } else {
            PyErr_Restore(type, value, traceback);
        }
    }
    if (!result)
        return -1;
    outer = PySequence_Fast(result,
                            "game.terminal_values must return player sequences");
    Py_DECREF(result);
    if (!outer)
        return -1;
    if (PySequence_Fast_GET_SIZE(outer) != (Py_ssize_t)players) {
        PyErr_SetString(PyExc_ValueError,
                        "terminal_values returned the wrong player count");
        Py_DECREF(outer);
        return -1;
    }
    for (player = 0u; player < players; ++player) {
        PyObject *values = PySequence_Fast(
            PySequence_Fast_GET_ITEM(outer, player),
            "terminal_values entries must be sequences");
        Py_ssize_t combo;
        if (!values) {
            Py_DECREF(outer);
            return -1;
        }
        if (PySequence_Fast_GET_SIZE(values) != (Py_ssize_t)out[player].n) {
            PyErr_SetString(PyExc_ValueError,
                            "terminal_values returned the wrong combo count");
            Py_DECREF(values);
            Py_DECREF(outer);
            return -1;
        }
        for (combo = 0; combo < (Py_ssize_t)out[player].n; ++combo) {
            out[player].v[combo] = PyFloat_AsDouble(
                PySequence_Fast_GET_ITEM(values, combo));
            if (PyErr_Occurred()) {
                Py_DECREF(values);
                Py_DECREF(outer);
                return -1;
            }
        }
        Py_DECREF(values);
    }
    Py_DECREF(outer);
    return 0;
}

static void py_solver_v3_destroy_context(py_solver_v3_context_t *ctx)
{
    if (!ctx)
        return;
    pe_solver_destroy(ctx->solver);
    Py_XDECREF(ctx->states);
    Py_XDECREF(ctx->game_object);
    free(ctx);
}

static void py_solver_v3_capsule_destructor(PyObject *capsule)
{
    py_solver_v3_context_t *ctx = (py_solver_v3_context_t *)PyCapsule_GetPointer(
        capsule, "poker_eval.solver_v3");
    if (ctx)
        py_solver_v3_destroy_context(ctx);
}

static py_solver_v3_context_t *py_solver_v3_get_context(PyObject *capsule)
{
    return (py_solver_v3_context_t *)PyCapsule_GetPointer(
        capsule, "poker_eval.solver_v3");
}

static PyObject *py_solver_v3_create(PyObject *self, PyObject *args,
                                     PyObject *kwargs)
{
    PyObject *root;
    PyObject *game_object;
    unsigned int players = 2u;
    unsigned int combos = 1u;
    unsigned long long max_iterations = 1000ull;
    double target = 0.0;
    unsigned long long interval = 0ull;
    unsigned long long expected_infosets = 1ull;
    unsigned int expected_actions = 2u;
    static const char *kwlist[] = {"root", "game", "players", "combos",
                                   "max_iterations", "target_exploitability_mbb",
                                   "exploitability_interval", "expected_infosets",
                                   "expected_actions", NULL};
    py_solver_v3_context_t *ctx;
    pe_solver_config_t config;
    pe_solver_deps_t deps;
    PyObject *capsule;
    int has_chance;
    (void)self;
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "OO|IIKdKKI", (char **)(void *)kwlist, &root, &game_object,
            &players, &combos, &max_iterations, &target, &interval,
            &expected_infosets, &expected_actions))
        return NULL;
    if (players < 2u || players > PE_SOLVER_MAX_PLAYERS || combos == 0u ||
        combos > UINT16_MAX || expected_actions < 1u) {
        PyErr_SetString(PyExc_ValueError,
                        "players, combos and expected_actions are invalid; combos must fit uint16_t");
        return NULL;
    }
    if (!PyObject_HasAttrString(game_object, "is_terminal") ||
        !PyObject_HasAttrString(game_object, "acting_player") ||
        !PyObject_HasAttrString(game_object, "action_count") ||
        !PyObject_HasAttrString(game_object, "infoset_key") ||
        !PyObject_HasAttrString(game_object, "apply_action") ||
        !PyObject_HasAttrString(game_object, "terminal_values")) {
        PyErr_SetString(PyExc_TypeError,
                        "game must implement the v3 callback methods");
        return NULL;
    }
    has_chance = PyObject_HasAttrString(game_object, "is_chance") ||
                 PyObject_HasAttrString(game_object, "chance_outcome_count") ||
                 PyObject_HasAttrString(game_object, "chance_outcome_weight") ||
                 PyObject_HasAttrString(game_object, "apply_chance");
    if (has_chance &&
        (!PyObject_HasAttrString(game_object, "is_chance") ||
         !PyObject_HasAttrString(game_object, "chance_outcome_count") ||
         !PyObject_HasAttrString(game_object, "chance_outcome_weight") ||
         !PyObject_HasAttrString(game_object, "apply_chance"))) {
        PyErr_SetString(PyExc_TypeError,
                        "chance games must implement all chance callback methods");
        return NULL;
    }
    ctx = (py_solver_v3_context_t *)calloc(1u, sizeof(*ctx));
    if (!ctx) {
        PyErr_NoMemory();
        return NULL;
    }
    ctx->game_object = game_object;
    Py_INCREF(game_object);
    ctx->states = PyList_New(0);
    if (!ctx->states || PyList_Append(ctx->states, root) != 0) {
        py_solver_v3_destroy_context(ctx);
        return NULL;
    }
    memset(&ctx->game, 0, sizeof(ctx->game));
    ctx->game.root = root;
    ctx->game.user = ctx;
    ctx->game.player_count = (uint8_t)players;
    ctx->game.combo_count = (uint16_t)combos;
    ctx->game.is_terminal = py_solver_v3_is_terminal;
    ctx->game.acting_player = py_solver_v3_acting_player;
    ctx->game.action_count = py_solver_v3_action_count;
    ctx->game.infoset_key = py_solver_v3_infoset_key;
    ctx->game.apply_action = py_solver_v3_apply_action;
    ctx->game.terminal_values = py_solver_v3_terminal_values;
    ctx->game.release_state = py_solver_v3_release_state;
    if (PyObject_HasAttrString(game_object, "combo_compatible"))
        ctx->game.combo_compatible = py_solver_v3_combo_compatible;
    if (has_chance) {
        ctx->game.is_chance = py_solver_v3_is_chance;
        ctx->game.chance_outcome_count = py_solver_v3_chance_outcome_count;
        ctx->game.chance_outcome_weight = py_solver_v3_chance_outcome_weight;
        ctx->game.apply_chance = py_solver_v3_apply_chance;
    }
    if (PyObject_HasAttrString(game_object, "strategy"))
        ctx->game.strategy = py_solver_v3_strategy_callback;
    config = pe_solver_config_default();
    config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
    config.max_iterations = (uint64_t)max_iterations;
    config.target_exploitability_mbb = target;
    config.exploitability_interval = (uint64_t)interval;
    config.problem.expected_infosets = (uint64_t)expected_infosets;
    config.problem.expected_actions = (uint16_t)expected_actions;
    config.problem.expected_combos = (uint16_t)combos;
    deps = pe_solver_deps_default();
    deps.vector_game = &ctx->game;
    ctx->solver = pe_solver_create(&config, &deps);
    if (!ctx->solver) {
        py_solver_v3_destroy_context(ctx);
        PyErr_SetString(PyExc_RuntimeError, "could not create v3 solver");
        return NULL;
    }
    capsule = PyCapsule_New(ctx, "poker_eval.solver_v3",
                            py_solver_v3_capsule_destructor);
    if (!capsule) {
        py_solver_v3_destroy_context(ctx);
        return NULL;
    }
    return capsule;
}

static PyObject *py_solver_v3_run(PyObject *self, PyObject *args)
{
    PyObject *capsule;
    py_solver_v3_context_t *ctx;
    pe_solver_status_t status;
    (void)self;
    if (!PyArg_ParseTuple(args, "O", &capsule))
        return NULL;
    ctx = py_solver_v3_get_context(capsule);
    if (!ctx)
        return NULL;
    status = pe_solver_run(ctx->solver);
    if (status != PE_SOLVER_OK) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_RuntimeError, "v3 solver failed with status %d",
                         (int)status);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *py_solver_v3_progress(PyObject *self, PyObject *args)
{
    PyObject *capsule;
    py_solver_v3_context_t *ctx;
    pe_progress_t progress;
    (void)self;
    if (!PyArg_ParseTuple(args, "O", &capsule))
        return NULL;
    ctx = py_solver_v3_get_context(capsule);
    if (!ctx || pe_solver_progress(ctx->solver, &progress) != PE_SOLVER_OK)
        return NULL;
    return Py_BuildValue("{s:K,s:K,s:d,s:i,s:i,s:i}", "iteration",
                         (unsigned long long)progress.iteration,
                         "total_iterations",
                         (unsigned long long)progress.total_iterations,
                         "fraction", progress.fraction, "running",
                         progress.running, "paused", progress.paused,
                         "complete", progress.complete);
}

static PyObject *py_solver_v3_strategy_query(PyObject *self, PyObject *args)
{
    PyObject *capsule;
    unsigned long long infoset;
    py_solver_v3_context_t *ctx;
    pe_strategy_query_t query;
    pe_strategy_view_t view;
    pe_solver_status_t status;
    PyObject *result;
    size_t i;
    (void)self;
    if (!PyArg_ParseTuple(args, "OK", &capsule, &infoset))
        return NULL;
    if (infoset > UINT32_MAX)
        return PyErr_Format(PyExc_ValueError, "infoset is out of range");
    ctx = py_solver_v3_get_context(capsule);
    if (!ctx)
        return NULL;
    query.infoset = (uint32_t)infoset;
    status = pe_solver_strategy(ctx->solver, &query, &view);
    if (status != PE_SOLVER_OK) {
        PyErr_Format(PyExc_RuntimeError,
                     "v3 strategy query failed with status %d", (int)status);
        return NULL;
    }
    result = PyList_New(view.count);
    if (!result)
        return NULL;
    for (i = 0u; i < view.count; ++i)
        PyList_SET_ITEM(result, i, PyFloat_FromDouble(view.values[i]));
    return result;
}

static PyObject *py_solver_v3_metrics(PyObject *self, PyObject *args)
{
    PyObject *capsule;
    py_solver_v3_context_t *ctx;
    pe_metrics_t metrics;
    PyObject *gaps;
    PyObject *result;
    uint8_t player;
    (void)self;
    if (!PyArg_ParseTuple(args, "O", &capsule))
        return NULL;
    ctx = py_solver_v3_get_context(capsule);
    if (!ctx || pe_solver_metrics(ctx->solver, &metrics) != PE_SOLVER_OK)
        return NULL;
    gaps = PyList_New(metrics.num_players);
    if (!gaps)
        return NULL;
    for (player = 0u; player < metrics.num_players; ++player)
        PyList_SET_ITEM(gaps, player, PyFloat_FromDouble(metrics.br_gap[player]));
    result = Py_BuildValue("{s:d,s:d,s:d,s:i,s:i,s:O}",
                           "exploitability_raw", metrics.exploitability_raw,
                           "exploitability_mbb_per_game",
                           metrics.exploitability_mbb_per_game,
                           "big_blind", metrics.big_blind, "guarantee",
                           (int)metrics.guarantee, "players", metrics.num_players,
                           "br_gap", gaps);
    Py_DECREF(gaps);
    return result;
}

static PyMethodDef base_methods[] = {
    {"eval_hand", (PyCFunction)eval_hand, METH_VARARGS | METH_KEYWORDS, doc_eval_hand},
    {"poker_eval", (PyCFunction)(void (*)(void))poker_eval, METH_VARARGS | METH_KEYWORDS, doc_poker_eval},
    {"evaln", (PyCFunction)poker_evaln, METH_VARARGS, doc_poker_evaln},
    {"string2card", (PyCFunction)string2card, METH_VARARGS, doc_string2card},
    {"card2string", (PyCFunction)card2string, METH_VARARGS, doc_card2string},
    {"omaha_hand_instantiate", (PyCFunction)py_omaha_hand_instantiate, METH_VARARGS, "Instantiate Omaha hands based on a hand string and dead cards."},
    {"stud_hand_instantiate", (PyCFunction)py_stud_hand_instantiate, METH_VARARGS, "Instantiate Stud hands based on a hand string, game total cards, and dead cards."},
    {"calculate_equity_for_ranges", (PyCFunction)py_calculate_equity_for_ranges, METH_VARARGS, "Calculates equity for player ranges."},
    {"calculate_multiway_equity", (PyCFunction)(void (*)(void))calculate_multiway_equity, METH_VARARGS | METH_KEYWORDS, "Calculates multiway equity with side pots."},
    {"convert_card_strings_to_mask_value", (PyCFunction)py_convert_card_strings_to_mask_value, METH_VARARGS, "Converts a list of card strings to its uint64_t card mask value."},
    {"solver_v3_create", (PyCFunction)(void (*)(void))py_solver_v3_create, METH_VARARGS | METH_KEYWORDS, "Create a Python-callback-backed v3 solver."},
    {"solver_v3_run", (PyCFunction)py_solver_v3_run, METH_VARARGS, "Run a v3 solver capsule."},
    {"solver_v3_progress", (PyCFunction)py_solver_v3_progress, METH_VARARGS, "Read v3 solver progress."},
    {"solver_v3_strategy", (PyCFunction)py_solver_v3_strategy_query, METH_VARARGS, "Read a v3 average strategy."},
    {"solver_v3_metrics", (PyCFunction)py_solver_v3_metrics, METH_VARARGS, "Read v3 exploitability metrics."},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef pokereval_3_11 =
    {
        PyModuleDef_HEAD_INIT,
        "pokereval_3_11", /* name of module */
        "",               /* module documentation, may be NULL */
        -1,               /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        base_methods,
        NULL, /* m_slots */
        NULL, /* m_traverse */
        NULL, /* m_clear */
        NULL  /* m_free */
};

PyMODINIT_FUNC PyInit__pokereval_3_11(void)
{
  return PyModule_Create(&pokereval_3_11);
}

/* Module definition for pypokereval (Python 3 compatibility) */
static struct PyModuleDef pypokereval_module =
    {
        PyModuleDef_HEAD_INIT,
        "pypokereval",  /* name of module */
        "",             /* module documentation, may be NULL */
        -1,             /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        base_methods,
        NULL,           /* m_slots */
        NULL,           /* m_traverse */
        NULL,           /* m_clear */
        NULL            /* m_free */
};

PyMODINIT_FUNC PyInit_pypokereval(void)
{
  return PyModule_Create(&pypokereval_module);
}

#pragma GCC diagnostic pop
