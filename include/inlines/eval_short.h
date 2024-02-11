#ifndef __EVAL_SHORT_H__
#define __EVAL_SHORT_H__

#include "poker_defs.h"
#include "inlines/eval.h"
#include <stdio.h>
#include "rules_short.h"

#define SC ShortDeck_CardMask_CLUBS(cards)
#define SD ShortDeck_CardMask_DIAMONDS(cards)
#define SH ShortDeck_CardMask_HEARTS(cards)
#define SS ShortDeck_CardMask_SPADES(cards)

static uint32 ShortHandTypeMap[StdRules_HandType_LAST+1] = {
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_NOPAIR), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_ONEPAIR), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_TWOPAIR), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_TRIPS), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_STRAIGHT), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_FLUSH), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_FULLHOUSE), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_QUADS), 
  HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH)
};


static inline HandVal 
ShortDeck_ShortRules_EVAL_N( ShortDeck_CardMask cards, int n_cards )
{
  StdDeck_CardMask stdcards;
  HandVal retval;
  uint32 ranks, n_ranks, stdHandType, stdCards;

  /* The strategy here is to first use the standard evaluator, and then
   * make adjustments for the asian stud rules, which are 
   * "flush beats full house" and "A-7-8-9-T straight".  
   * We make use of the assumption that the standard card mask and Short
   * card mask have same layout.  
   */
  stdcards = cards;
  retval = StdDeck_StdRules_EVAL_N(stdcards, n_cards);
  stdHandType = HandVal_HANDTYPE(retval);
  stdCards    = HandVal_CARDS(retval);



  ranks = SC | SD | SH | SS;
  n_ranks = nBitsTable[ranks];



  switch (stdHandType) {
  case StdRules_HandType_QUADS:
  case StdRules_HandType_FLUSH:
    if (n_ranks >= 5) {
      if ((SS & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
        return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
          + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
      else if ((SC & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
        return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
          + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
      else if ((SD & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
        return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
          + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
      else if ((SH & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
        return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
          + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
    };
    return ShortHandTypeMap[stdHandType] + stdCards;
    break;

  case StdRules_HandType_STFLUSH:
  case StdRules_HandType_STRAIGHT:
    return ShortHandTypeMap[stdHandType] + stdCards;
    break;

  case StdRules_HandType_FULLHOUSE: 
    if (n_ranks >= 5) {
      if (nBitsTable[SS] >= 5) {
        if ((SS & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
            + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
        else 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_FLUSH) 
            + topFiveCardsTable[SS];
      }
      else if (nBitsTable[SC] >= 5) {
        if ((SC & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
            + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
        else 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_FLUSH) 
            + topFiveCardsTable[SC];
      }
      else if (nBitsTable[SD] >= 5) {
        if ((SD & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
            + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
        else 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_FLUSH) 
            + topFiveCardsTable[SD];
      }
      else if (nBitsTable[SH] >= 5) {
        if ((SH & ShortRules_9_STRAIGHT) == ShortRules_9_STRAIGHT) 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STFLUSH) 
            + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
        else 
          return HandVal_HANDTYPE_VALUE(ShortRules_HandType_FLUSH) 
            + topFiveCardsTable[SH];
      }
    };
    return HandVal_HANDTYPE_VALUE(ShortRules_HandType_FULLHOUSE) + stdCards;
    break;

  case StdRules_HandType_TRIPS:
  case StdRules_HandType_TWOPAIR:
  case StdRules_HandType_ONEPAIR:
  case StdRules_HandType_NOPAIR:
    if ((ranks & ShortRules_9_STRAIGHT) ==  ShortRules_9_STRAIGHT) 
      return HandVal_HANDTYPE_VALUE(ShortRules_HandType_STRAIGHT) 
        + HandVal_TOP_CARD_VALUE(ShortDeck_Rank_9);
    else
      return ShortHandTypeMap[stdHandType] + stdCards;
  };

  return 0;
}

#undef SC
#undef SH
#undef SD
#undef SS

#endif

