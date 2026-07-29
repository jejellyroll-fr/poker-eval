#ifndef __RULES_MANILA_H__
#define __RULES_MANILA_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

/*
 * Manila Poker Rules
 * - 32-card deck (8-A in each suit)
 * - Modified hand rankings: Flush beats Full House
 * - Aces are high only (no A-2-3-4-5 straights)
 * - Must use both hole cards + exactly 3 community cards
 */

/* Manila-specific hand type values */
#define ManilaRules_HandType_NOPAIR   0
#define ManilaRules_HandType_ONEPAIR  1
#define ManilaRules_HandType_TWOPAIR  2
#define ManilaRules_HandType_TRIPS    3
#define ManilaRules_HandType_STRAIGHT 4
#define ManilaRules_HandType_FLUSH    5  /* FLUSH beats FULLHOUSE in Manila */
#define ManilaRules_HandType_FULLHOUSE 6 /* FULLHOUSE loses to FLUSH in Manila */
#define ManilaRules_HandType_QUADS    7
#define ManilaRules_HandType_STFLUSH  8
#define ManilaRules_HandType_FIRST    ManilaRules_HandType_NOPAIR
#define ManilaRules_HandType_LAST     ManilaRules_HandType_STFLUSH
#define ManilaRules_HandType_COUNT    9

/* Manila-specific straight definitions (only high straights) */
#define ManilaRules_N_STRAIGHTS       6
/* Possible Manila straights: 8-9-T-J-Q, 9-T-J-Q-K, T-J-Q-K-A */
/* Plus wheel variations but only high (no low ace straights) */

/* Define the current rule set when RULES_MANILA is active */
#ifdef RULES_MANILA

#if defined(HandType_NOPAIR)
#include "rules_undef.h"
#endif

#define HandType_NOPAIR   ManilaRules_HandType_NOPAIR
#define HandType_ONEPAIR  ManilaRules_HandType_ONEPAIR
#define HandType_TWOPAIR  ManilaRules_HandType_TWOPAIR
#define HandType_TRIPS    ManilaRules_HandType_TRIPS
#define HandType_STRAIGHT ManilaRules_HandType_STRAIGHT
#define HandType_FLUSH    ManilaRules_HandType_FLUSH
#define HandType_FULLHOUSE ManilaRules_HandType_FULLHOUSE
#define HandType_QUADS    ManilaRules_HandType_QUADS
#define HandType_STFLUSH  ManilaRules_HandType_STFLUSH
#define HandType_FIRST    ManilaRules_HandType_FIRST
#define HandType_LAST     ManilaRules_HandType_LAST
#define HandType_COUNT    ManilaRules_HandType_COUNT

#define CurRules ManilaRules

#endif /* RULES_MANILA */

/* Manila-specific evaluation functions */
extern int ManilaRules_HandVal_toString(HandVal handval, char *outString);
extern int ManilaRules_HandVal_print(HandVal handval);

/* Manila hand ranking validation */
extern int Manila_IsValidStraight(int ranks);
extern HandVal Manila_EvaluateHand(StdDeck_CardMask cards, int num_cards);

#endif /* __RULES_MANILA_H__ */
