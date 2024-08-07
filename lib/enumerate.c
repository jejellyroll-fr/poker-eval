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

#include "poker_defs.h"
#include "inlines/eval.h"
#include "inlines/eval_low.h"
#include "inlines/eval_low8.h"
#include "inlines/eval_low27.h"
#include "inlines/eval_joker_low.h"
#include "inlines/eval_omaha.h"

#include "deck_std.h"
#include "rules_std.h"


#include "enumerate.h"
#include "enumdefs.h"
#include "deck_short.h"
#include "rules_short.h"
#include "inlines/eval_short.h"

static enum_gameparams_t enum_gameparams[] = {
  /* must be in same order as enum_game_t */
  { game_holdem,    2,  2,  5, 0, 1, "Holdem Hi" },
  { game_holdem8,   2,  2,  5, 1, 1, "Holdem Hi/Low 8-or-better" },
  { game_omaha,     4,  4,  5, 0, 1, "Omaha Hi" },
  { game_omaha5,    5,  5,  5, 0, 1, "Omaha Hi 5cards" },
  { game_omaha6,    6,  6,  5, 0, 1, "Omaha Hi 6cards" },
  { game_omaha8,    4,  4,  5, 1, 1, "Omaha Hi/Low 8-or-better" },
  { game_omaha85,   5,  5,  5, 1, 1, "Omaha 5cards Hi/Low 8-or-better" },
  { game_7stud,     3,  7,  0, 0, 1, "7-card Stud Hi" },
  { game_7stud8,    3,  7,  0, 1, 1, "7-card Stud Hi/Low 8-or-better" },
  { game_7studnsq,  3,  7,  0, 1, 1, "7-card Stud Hi/Low no qualifier" },
  { game_razz,      3,  7,  0, 1, 0, "Razz (7-card Stud A-5 Low)" },
  { game_5draw,     0,  5,  0, 0, 1, "5-card Draw Hi with joker" },
  { game_5draw8,    0,  5,  0, 1, 1, "5-card Draw Hi/Low 8-or-better with joker" },
  { game_5drawnsq,  0,  5,  0, 1, 1, "5-card Draw Hi/Low no qualifier with joker" },
  { game_lowball,   0,  5,  0, 1, 0, "5-card Draw A-5 Lowball with joker" },
  { game_lowball27, 0,  5,  0, 1, 0, "5-card Draw 2-7 Lowball" },
  { game_sdholdem,  2,  2,  5, 0, 1, "ShortDeck Holdem NL Hi" },
  { game_doubleflop_holdem, 2, 2, 10, 0, 1, "Double Flop Holdem Hi" },

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

#define INNER_LOOP(evalwrap)						\
    do {								\
      int i;								\
      HandVal hival[ENUM_MAXPLAYERS];					\
      LowHandVal loval[ENUM_MAXPLAYERS];				\
      HandVal besthi = HandVal_NOTHING;					\
      LowHandVal bestlo = LowHandVal_NOTHING;				\
      int hishare = 0;							\
      int loshare = 0;							\
      double hipot, lopot;						\
      /* find winning hands for high and low */				\
      for (i=0; i<npockets; i++) {					\
	int err;							\
        { evalwrap }							\
        if (err != 0)							\
          return 1000 + err;						\
        if (hival[i] != HandVal_NOTHING) {				\
          if (hival[i] > besthi) {					\
            besthi = hival[i];						\
            hishare = 1;						\
          } else if (hival[i] == besthi) {				\
            hishare++;							\
          }								\
        }								\
        if (loval[i] != LowHandVal_NOTHING) {				\
          if (loval[i] < bestlo) {					\
            bestlo = loval[i];						\
            loshare = 1;						\
          } else if (loval[i] == bestlo) {				\
            loshare++;							\
          }								\
        }								\
      }									\
      /* now award pot fractions to winning hands */			\
      if (bestlo != LowHandVal_NOTHING &&				\
          besthi != HandVal_NOTHING) {					\
        hipot = (hishare != 0) ? 0.5 / hishare : 0;			\
        lopot = (loshare != 0) ? 0.5 / loshare : 0;			\
      } else if (bestlo == LowHandVal_NOTHING &&			\
                 besthi != HandVal_NOTHING) {				\
        hipot = (hishare != 0) ? 1.0 / hishare : 0;			\
        lopot = 0;							\
      } else if (bestlo != LowHandVal_NOTHING &&			\
                 besthi == HandVal_NOTHING) {				\
        hipot = 0;							\
        lopot = (loshare != 0) ? 1.0 / loshare : 0;			\
      } else {								\
        hipot = lopot = 0;						\
      }									\
      for (i=0; i<npockets; i++) {					\
        double potfrac = 0;						\
        int H = 0, L = 0;						\
        if (hival[i] != HandVal_NOTHING) {				\
          if (hival[i] == besthi) {					\
            H = hishare;						\
            potfrac += hipot;						\
            if (hishare == 1)						\
              result->nwinhi[i]++;					\
            else							\
              result->ntiehi[i]++;					\
          } else {							\
            result->nlosehi[i]++;					\
          }								\
        }								\
        if (loval[i] != LowHandVal_NOTHING) {				\
          if (loval[i] == bestlo) {					\
            L = loshare;						\
            potfrac += lopot;						\
            if (loshare == 1)						\
              result->nwinlo[i]++;					\
            else							\
              result->ntielo[i]++;					\
          } else {							\
            result->nloselo[i]++;					\
          }								\
        }								\
        result->nsharehi[i][H]++;					\
        result->nsharelo[i][L]++;					\
        result->nshare[i][H][L]++;					\
        if (potfrac > 0.99)						\
          result->nscoop[i]++;						\
        result->ev[i] += potfrac;					\
      }									\
      if (result->ordering != NULL) {					\
        if (result->ordering->mode == enum_ordering_mode_hi) {		\
          int hiranks[ENUM_ORDERING_MAXPLAYERS];			\
          ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);\
          ENUM_ORDERING_INCREMENT(result->ordering, npockets, hiranks);	\
        }								\
        if (result->ordering->mode == enum_ordering_mode_lo) {		\
          int loranks[ENUM_ORDERING_MAXPLAYERS];			\
          ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks);\
          ENUM_ORDERING_INCREMENT(result->ordering, npockets, loranks);	\
        }								\
        if (result->ordering->mode == enum_ordering_mode_hilo) {	\
          int hiranks[ENUM_ORDERING_MAXPLAYERS_HILO];			\
          int loranks[ENUM_ORDERING_MAXPLAYERS_HILO];			\
          ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);\
          ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks);\
          ENUM_ORDERING_INCREMENT_HILO(result->ordering, npockets,	\
                                       hiranks, loranks);		\
        }								\
      }									\
      result->nsamples++;						\
    } while (0);


#define INNER_LOOP_HOLDEM						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);		\
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);			\
    loval[i] = LowHandVal_NOTHING;					\
    err = 0;								\
  })


#define INNER_LOOP_SDHOLDEM						\
  INNER_LOOP({								\
    ShortDeck_CardMask _hand;						\
    ShortDeck_CardMask _finalBoard;					\
    ShortDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    ShortDeck_CardMask_OR(_hand, pockets[i], _finalBoard);		\
    hival[i] = ShortDeck_ShortRules_EVAL_N(_hand, 7);			\
    loval[i] = LowHandVal_NOTHING;					\
    err = 0;								\
  })

#define INNER_LOOP_HOLDEM8						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    StdDeck_CardMask_OR(_hand, pockets[i], _finalBoard);		\
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);			\
    loval[i] = StdDeck_Lowball8_EVAL(_hand, 7);				\
    err = 0;								\
  })

#define INNER_LOOP_OMAHA						\
  INNER_LOOP({								\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard,		\
                                   &hival[i], NULL);			\
    loval[i] = LowHandVal_NOTHING;					\
  })

#define INNER_LOOP_OMAHA5						\
  INNER_LOOP({								\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard,		\
                                   &hival[i], NULL);			\
    loval[i] = LowHandVal_NOTHING;					\
  })

#define INNER_LOOP_OMAHA6						\
  INNER_LOOP({								\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard,		\
                                   &hival[i], NULL);			\
    loval[i] = LowHandVal_NOTHING;					\
  })

#define INNER_LOOP_OMAHA8						\
  INNER_LOOP({								\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard,		\
                                   &hival[i], &loval[i]);		\
  })

#define INNER_LOOP_OMAHA85						\
  INNER_LOOP({								\
    StdDeck_CardMask _finalBoard;					\
    StdDeck_CardMask_OR(_finalBoard, board, sharedCards);		\
    err = StdDeck_OmahaHiLow8_EVAL(pockets[i], _finalBoard,		\
                                   &hival[i], &loval[i]);		\
  })

#define INNER_LOOP_7STUD						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);			\
    loval[i] = LowHandVal_NOTHING;					\
    err = 0;								\
  })

#define INNER_LOOP_7STUD8						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);			\
    loval[i] = StdDeck_Lowball8_EVAL(_hand, 7);				\
    err = 0;								\
  })

#define INNER_LOOP_7STUDNSQ						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = StdDeck_StdRules_EVAL_N(_hand, 7);			\
    loval[i] = StdDeck_Lowball_EVAL(_hand, 7);				\
    err = 0;								\
  })

#define INNER_LOOP_RAZZ							\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = HandVal_NOTHING;						\
    loval[i] = StdDeck_Lowball_EVAL(_hand, 7);				\
    err = 0;								\
  })

#define INNER_LOOP_5DRAW						\
  INNER_LOOP({								\
    JokerDeck_CardMask _hand;						\
    JokerDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);			\
    loval[i] = LowHandVal_NOTHING;					\
    err = 0;								\
  })

#define INNER_LOOP_5DRAW8						\
  INNER_LOOP({								\
    JokerDeck_CardMask _hand;						\
    JokerDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);			\
    loval[i] = JokerDeck_Lowball8_EVAL(_hand, 5);			\
    err = 0;								\
  })

#define INNER_LOOP_5DRAWNSQ						\
  INNER_LOOP({								\
    JokerDeck_CardMask _hand;						\
    JokerDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = JokerDeck_JokerRules_EVAL_N(_hand, 5);			\
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);			\
    err = 0;								\
  })

#define INNER_LOOP_LOWBALL						\
  INNER_LOOP({								\
    JokerDeck_CardMask _hand;						\
    JokerDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = HandVal_NOTHING;						\
    loval[i] = JokerDeck_Lowball_EVAL(_hand, 5);			\
    err = 0;								\
  })

#define INNER_LOOP_LOWBALL27						\
  INNER_LOOP({								\
    StdDeck_CardMask _hand;						\
    StdDeck_CardMask_OR(_hand, pockets[i], unsharedCards[i]);		\
    hival[i] = HandVal_NOTHING;						\
    loval[i] = StdDeck_Lowball27_EVAL_N(_hand, 5);			\
    err = 0;								\
  })


#define INNER_LOOP_DOUBLEFLOP_HOLDEM(evalwrap1, evalwrap2)                      \
do {                                                                            \
    int i;                                                                      \
    HandVal hival1[ENUM_MAXPLAYERS], hival2[ENUM_MAXPLAYERS];                   \
    HandVal besthi1 = HandVal_NOTHING, besthi2 = HandVal_NOTHING;               \
    int hishare1 = 0, hishare2 = 0;                                             \
    double hipot1, hipot2;                                                      \
                                                                                \
    for (i=0; i<npockets; i++) {                                                \
        int err1, err2;                                                         \
        /* Evaluate hand for first board */                                     \
        { evalwrap1 }                                                           \
        /* Evaluate hand for second board */                                    \
        { evalwrap2 }                                                           \
        /* Determine the best hand and share for both boards */                 \
        if (hival1[i] > besthi1) {                                              \
            besthi1 = hival1[i];                                                \
            hishare1 = 1;                                                       \
        } else if (hival1[i] == besthi1) {                                      \
            hishare1++;                                                         \
        }                                                                       \
        if (hival2[i] > besthi2) {                                              \
            besthi2 = hival2[i];                                                \
            hishare2 = 1;                                                       \
        } else if (hival2[i] == besthi2) {                                      \
            hishare2++;                                                         \
        }                                                                       \
    }                                                                           \
    /* Pot distribution for each board */                                       \
    hipot1 = besthi1 != HandVal_NOTHING ? 1.0 / hishare1 : 0;                   \
    hipot2 = besthi2 != HandVal_NOTHING ? 1.0 / hishare2 : 0;                   \
                                                                                \
    for (i=0; i<npockets; i++) {                                                \
        double potfrac1 = 0, potfrac2 = 0;                                      \
        if (hival1[i] == besthi1) { potfrac1 += hipot1; }                       \
        if (hival2[i] == besthi2) { potfrac2 += hipot2; }                       \
        /* Combine results from both boards */                                  \
        result->ev[i] += (potfrac1 + potfrac2) / 2;                             \
        /* Update win/tie/loss and scoop counters */                            \
        if (hival1[i] == besthi1 && hival2[i] == besthi2) {                     \
            if (hishare1 == 1 && hishare2 == 1) {                               \
                result->nscoop[i]++;  /* Scoop counter updated for winning both boards */  \
            }                                                                   \
        }                                                                       \
    }                                                                           \
    /* Update ordering histogram for hihi mode if applicable */                 \
    if (result->ordering != NULL && result->ordering->mode == enum_ordering_mode_hihi) { \
        int hiranks1[ENUM_ORDERING_MAXPLAYERS], hiranks2[ENUM_ORDERING_MAXPLAYERS]; \
        ENUM_ORDERING_RANK_HI(hival1, HandVal_NOTHING, npockets, hiranks1);     \
        ENUM_ORDERING_RANK_HI(hival2, HandVal_NOTHING, npockets, hiranks2);     \
        for (i = 0; i < npockets; i++) {                                        \
            int rank1 = hiranks1[i], rank2 = hiranks2[i];                       \
            int combinedRank = rank1 * ENUM_ORDERING_MAXPLAYERS + rank2;        \
            if (combinedRank < result->ordering->nentries) {                    \
                result->ordering->hist[combinedRank]++;                         \
            }                                                                   \
        }                                                                       \
    }                                                                           \
    result->nsamples++;                                                         \
} while (0)








int 
enumExhaustive(enum_game_t game, StdDeck_CardMask pockets[],
               StdDeck_CardMask board, StdDeck_CardMask dead,
               int npockets, int nboard, int orderflag,
               enum_result_t *result) {
  int i;

  enumResultClear(result);
  if (npockets > ENUM_MAXPLAYERS)
    return 1;
  if (orderflag) {
    enum_ordering_mode_t mode;
    switch (game) {
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
    case game_7stud8:
    case game_7studnsq:
    case game_5draw8:
    case game_5drawnsq:
      mode = enum_ordering_mode_hilo;
      break;
    case game_doubleflop_holdem:
      mode = enum_ordering_mode_hihi;
    default:
      return 1;
    }
    if (enumResultAlloc(result, npockets, mode))
      return 1;
  }

  if (game == game_holdem) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM;
    } else {
      return 1;
    }

  } else if (game == game_sdholdem) {
    ShortDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(ShortDeck, sharedCards, dead, INNER_LOOP_SDHOLDEM);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(ShortDeck, sharedCards, dead, INNER_LOOP_SDHOLDEM);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(ShortDeck, sharedCards, dead, INNER_LOOP_SDHOLDEM);
    } else if (nboard == 5) {
      ShortDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_SDHOLDEM;
    } else {
      return 1;
    }

  } else if (game == game_doubleflop_holdem) {
      StdDeck_CardMask sharedCards1, sharedCards2; 
      if (nboard == 0) {
          // Enumérer le premier ensemble de cartes communautaires
          DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards1, dead, {
              // énumérer le second ensemble de cartes communautaires
              DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards2, dead, {
                  INNER_LOOP_DOUBLEFLOP_HOLDEM({
                      StdDeck_CardMask _finalBoard1;
                      StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
                      StdDeck_CardMask _hand1;
                      StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
                      hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
                      err1 = 0;
                  }, {
                      StdDeck_CardMask _finalBoard2;
                      StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
                      StdDeck_CardMask _hand2;
                      StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
                      hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
                      err2 = 0;
                  });
              });
          });
      } else if (nboard == 3) {
          // Enumérer le premier ensemble de cartes communautaires
          DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards1, dead, {
              // énumérer le second ensemble de cartes communautaires
              DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards2, dead, {
                  INNER_LOOP_DOUBLEFLOP_HOLDEM({
                      StdDeck_CardMask _finalBoard1;
                      StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
                      StdDeck_CardMask _hand1;
                      StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
                      hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
                      err1 = 0;
                  }, {
                      StdDeck_CardMask _finalBoard2;
                      StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
                      StdDeck_CardMask _hand2;
                      StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
                      hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
                      err2 = 0;
                  });
              });
          });
      } else if (nboard == 4) {
          // Enumérer le premier ensemble de cartes communautaires
          DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards1, dead, {
              // énumérer le second ensemble de cartes communautaires
              DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards2, dead, {
                  INNER_LOOP_DOUBLEFLOP_HOLDEM({
                      StdDeck_CardMask _finalBoard1;
                      StdDeck_CardMask_OR(_finalBoard1, board, sharedCards1);
                      StdDeck_CardMask _hand1;
                      StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
                      hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
                      err1 = 0;
                  }, {
                      StdDeck_CardMask _finalBoard2;
                      StdDeck_CardMask_OR(_finalBoard2, board, sharedCards2);
                      StdDeck_CardMask _hand2;
                      StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
                      hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
                      err2 = 0;
                  });
              });
          });
      } else if (nboard == 5) {
          StdDeck_CardMask board1, board2;
          INNER_LOOP_DOUBLEFLOP_HOLDEM({
              StdDeck_CardMask _finalBoard1 = board1; 
              StdDeck_CardMask _hand1;
              StdDeck_CardMask_OR(_hand1, pockets[i], _finalBoard1);
              hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
              err1 = 0;
          }, {
              StdDeck_CardMask _finalBoard2 = board2; 
              StdDeck_CardMask _hand2;
              StdDeck_CardMask_OR(_hand2, pockets[i], _finalBoard2);
              hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
              err2 = 0;
          });          
      } else {
          return 1; 
      }




  } else if (game == game_holdem8) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM8);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM8);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_HOLDEM8);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM8;
    } else {
      return 1;
    }


  } else if (game == game_omaha) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA;
    } else {
      return 1;
    }

  } else if (game == game_omaha5) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA5);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA5);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA5);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA5;
    } else {
      return 1;
    }

  } else if (game == game_omaha6) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA6);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA6);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA6);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA6;
    } else {
      return 1;
    }

  } else if (game == game_omaha8) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA8);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA8);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA8);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA8;
    } else {
      return 1;
    }

  } else if (game == game_omaha85) {
    StdDeck_CardMask sharedCards;
    if (nboard == 0) {
      DECK_ENUMERATE_5_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA85);
    } else if (nboard == 3) {
      DECK_ENUMERATE_2_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA85);
    } else if (nboard == 4) {
      DECK_ENUMERATE_1_CARDS_D(StdDeck, sharedCards, dead, INNER_LOOP_OMAHA85);
    } else if (nboard == 5) {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA85;
    } else {
      return 1;
    }

  } else if (game == game_7stud) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_7STUD);

  } else if (game == game_7stud8) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_7STUD8);

  } else if (game == game_7studnsq) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_7STUDNSQ);

  } else if (game == game_razz) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_RAZZ);

  } else if (game == game_5draw) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(JokerDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_5DRAW);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_5draw8) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(JokerDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_5DRAW8);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_5drawnsq) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(JokerDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_5DRAWNSQ);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_lowball) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(JokerDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_LOWBALL);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_lowball27) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - StdDeck_numCards(pockets[i]);
    DECK_ENUMERATE_COMBINATIONS_D(StdDeck, unsharedCards,
                                  npockets, numToDeal,
                                  dead, INNER_LOOP_LOWBALL27);

  } else {
    return 1;
  }

  result->game = game;
  result->nplayers = npockets;
  result->sampleType = ENUM_EXHAUSTIVE;
  return 0;  
}

int 
enumSample(enum_game_t game, StdDeck_CardMask pockets[],
           StdDeck_CardMask board, StdDeck_CardMask dead,
           int npockets, int nboard, int niter, int orderflag,
           enum_result_t *result) {
  int i;
  int numCards;

  enumResultClear(result);
  if (npockets > ENUM_MAXPLAYERS)
    return 1;
  if (orderflag) {
    enum_ordering_mode_t mode;
    switch (game) {
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
    case game_7stud8:
    case game_7studnsq:
    case game_5draw8:
    case game_5drawnsq:
      mode = enum_ordering_mode_hilo;
      break;
    default:
      return 1;
    }
    if (enumResultAlloc(result, npockets, mode))
      return 1;
  }

  if (game == game_holdem) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_HOLDEM);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM;
      return 1;
    }

  } else if (game == game_sdholdem) {
    ShortDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(ShortDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_SDHOLDEM);
    } else {
      ShortDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_SDHOLDEM;
      return 1;
    }

  } else if (game == game_holdem8) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_HOLDEM8);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_HOLDEM8;
      return 1;
    }

  } else if (game == game_omaha) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_OMAHA);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA;
      return 1;
    }

  } else if (game == game_omaha5) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_OMAHA5);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA5;
      return 1;
    }

  } else if (game == game_omaha6) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_OMAHA6);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA6;
      return 1;
    }

  } else if (game == game_omaha8) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_OMAHA8);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA8;
      return 1;
    }

  } else if (game == game_omaha85) {
    StdDeck_CardMask sharedCards;
    numCards = 5 - nboard;
    if (numCards > 0) {
      DECK_MONTECARLO_N_CARDS_D(StdDeck, sharedCards, dead, numCards,
                                niter, INNER_LOOP_OMAHA85);
    } else {
      StdDeck_CardMask_RESET(sharedCards);
      INNER_LOOP_OMAHA85;
      return 1;
    }

  } else if (game == game_7stud) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUD);

  } else if (game == game_7stud8) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUD8);

  } else if (game == game_7studnsq) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_7STUDNSQ);

  } else if (game == game_razz) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 7 - StdDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_RAZZ);

  } else if (game == game_5draw) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(JokerDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_5DRAW);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_5draw8) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(JokerDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_5DRAW8);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_5drawnsq) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(JokerDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_5DRAWNSQ);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_lowball) {
#if 0
    /* we have a type problem: pockets should be JokerDeck_CardMask[] */
    JokerDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - JokerDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(JokerDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_LOWBALL);
#endif
    fprintf(stderr, "Joker enumeration not yet implemented\n");
    return 1;

  } else if (game == game_lowball27) {
    StdDeck_CardMask unsharedCards[ENUM_MAXPLAYERS];
    int numToDeal[ENUM_MAXPLAYERS];
    for (i=0; i<npockets; i++)
      numToDeal[i] = 5 - StdDeck_numCards(pockets[i]);
    DECK_MONTECARLO_PERMUTATIONS_D(StdDeck, unsharedCards,
                                   npockets, numToDeal,
                                   dead, niter, INNER_LOOP_LOWBALL27);

  } else {
    return 1;
  }

  result->game = game;
  result->nplayers = npockets;
  result->sampleType = ENUM_SAMPLE;
  return 0;  
}

void
enumResultClear(enum_result_t *result) {
  memset(result, 0, sizeof(enum_result_t));
}

void
enumResultFree(enum_result_t *result) {
  if (result->ordering != NULL) {
    if (result->ordering->hist != NULL)
      free(result->ordering->hist);
    free(result->ordering);
    result->ordering = NULL;
  }
}

int
enumResultAlloc(enum_result_t *result, int nplayers,
                enum_ordering_mode_t mode) {
  int nentries;
  switch (mode) {
  case enum_ordering_mode_hi:
  case enum_ordering_mode_lo:
    nentries = ENUM_ORDERING_NENTRIES(nplayers);
    break;
  case enum_ordering_mode_hilo:
    nentries = ENUM_ORDERING_NENTRIES_HILO(nplayers);
    break;
  case enum_ordering_mode_none:
    return 0;
  default:
    return 1;
  }
  if (nentries <= 0)
    return 1;
  result->ordering = (enum_ordering_t *) malloc(sizeof(enum_ordering_t));
  if (result->ordering == NULL)
    return 1;
  result->ordering->mode = mode;
  result->ordering->nplayers = nplayers;
  result->ordering->nentries = nentries;
  result->ordering->hist = (unsigned int *) calloc(nentries, sizeof(int));
  if (result->ordering->hist == NULL) {
    free(result->ordering);
    result->ordering = NULL;
    return 1;
  }
  return 0;
}

enum_gameparams_t *
enumGameParams(enum_game_t game) {
  if (game >= 0 && game < game_NUMGAMES)
    return &enum_gameparams[game];
  else
    return NULL;
}

static void
enumResultPrintOrdering(enum_result_t *result, int terse) {
  int i, k;

  if (!terse)
    printf("Histogram of relative hand ranks:\n");
  if (result->ordering->mode == enum_ordering_mode_hi ||
      result->ordering->mode == enum_ordering_mode_lo) {
    if (!terse) {
      for (k=0; k<result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf(" %8s\n", "Freq");
    } else
      printf("ORD %d %d:", result->ordering->mode, result->ordering->nplayers);
    for (i=0; i<result->ordering->nentries; i++) {
      if (result->ordering->hist[i] > 0) {
        for (k=0; k<result->ordering->nplayers; k++) {
          int rank = ENUM_ORDERING_DECODE_K(i,
                     result->ordering->nplayers, k);
          if (rank == result->ordering->nplayers)
            printf(" NQ");
          else {
            printf(" %2d", rank + 1);
          }
        }
        printf(" %8d", result->ordering->hist[i]);
        printf(terse ? "|" : "\n");
      }
    }
  } else if (result->ordering->mode == enum_ordering_mode_hilo) {
    if (!terse) {
      printf("HI:");
      for (k=0; k<result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf("  LO:");
      for (k=0; k<result->ordering->nplayers; k++)
        printf(" %2c", 'A' + k);
      printf(" %8s\n", "Freq");
    } else
      printf("ORD %d %d:", result->ordering->mode, result->ordering->nplayers);
    for (i=0; i<result->ordering->nentries; i++) {
      if (result->ordering->hist[i] > 0) {
        if (!terse)
          printf("   ");
        for (k=0; k<result->ordering->nplayers; k++) {
          int rankhi = ENUM_ORDERING_DECODE_HILO_K_HI(i,
                       result->ordering->nplayers, k);
          if (rankhi == result->ordering->nplayers)
            printf(" NQ");
          else {
            printf(" %2d", rankhi + 1);
          }
        }
        if (!terse)
          printf("     ");
        for (k=0; k<result->ordering->nplayers; k++) {
          int ranklo = ENUM_ORDERING_DECODE_HILO_K_LO(i,
                       result->ordering->nplayers, k);
          if (ranklo == result->ordering->nplayers)
            printf(" NQ");
          else {
            printf(" %2d", ranklo + 1);
          }
        }
        printf(" %8d", result->ordering->hist[i]);
        printf(terse ? "|" : "\n");
      }
    }
  }
  if (terse)
    printf("\n");
}


void
enumResultPrint(enum_result_t *result, StdDeck_CardMask pockets[],
                StdDeck_CardMask board) {
  int i;
  enum_gameparams_t *gp;
  int width;

  gp = enumGameParams(result->game);
  if (gp == NULL) {
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

  if (gp->haslopot && gp->hashipot) {
    printf("%*s %7s   %7s %7s %7s   %7s %7s %7s   %5s\n",
           -width, "cards", "scoop",
           "HIwin", "HIlos", "HItie",
           "LOwin", "LOlos", "LOtie",
           "EV");
    for (i=0; i<result->nplayers; i++) {
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
    
  } else {
    printf("%*s %7s %6s   %7s %6s   %7s %6s     %5s\n",
           -width, "cards", "win", "%win", "lose", "%lose", "tie", "%tie", "EV");
    if (gp->haslopot) {
      for (i=0; i<result->nplayers; i++) {
        printf("%*s %7d %6.2f   %7d %6.2f   %7d %6.2f     %5.3f\n",
               -width, DmaskString(StdDeck, pockets[i]),
               result->nwinlo[i], 100.0 * result->nwinlo[i] / result->nsamples,
               result->nloselo[i], 100.0 * result->nloselo[i] / result->nsamples,
               result->ntielo[i], 100.0 * result->ntielo[i] / result->nsamples,
               result->ev[i] / result->nsamples);
      }
    } else if (gp->hashipot) {
      for (i=0; i<result->nplayers; i++) {
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

void
enumResultPrintTerse(enum_result_t *result, StdDeck_CardMask pockets[],
                     StdDeck_CardMask board) {
  int i;

  printf("EV %d:", result->nplayers);
  for (i=0; i<result->nplayers; i++)
    printf(" %8.6f", result->ev[i] / result->nsamples);
  printf("\n");
  if (result->ordering != NULL)
    enumResultPrintOrdering(result, 1);
}
