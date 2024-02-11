#ifndef __RULES_SHORT_H__
#define __RULES_SHORT_H__

#include "pokereval_export.h"

// Define hand types for short deck  poker
#define ShortRules_HandType_NOPAIR    0
#define ShortRules_HandType_ONEPAIR   1
#define ShortRules_HandType_TWOPAIR   2
#define ShortRules_HandType_STRAIGHT  3
#define ShortRules_HandType_TRIPS     4
#define ShortRules_HandType_FULLHOUSE 5
#define ShortRules_HandType_FLUSH     6
#define ShortRules_HandType_QUADS     7
#define ShortRules_HandType_STFLUSH   8
#define ShortRules_HandType_FIRST     ShortRules_HandType_NOPAIR
#define ShortRules_HandType_LAST      ShortRules_HandType_STFLUSH
#define ShortRules_HandType_COUNT     9

// A mask representing the ranks of A-9 straight
#define ShortRules_9_STRAIGHT \
 ((1 << ShortDeck_Rank_ACE ) \
  | (1 << ShortDeck_Rank_6 ) \
  | (1 << ShortDeck_Rank_7 ) \
  | (1 << ShortDeck_Rank_8 ) \
  | (1 << ShortDeck_Rank_9 ))

// External declarations for hand type names, padded hand type names, signature card counts, and hand value related functions
extern const POKEREVAL_EXPORT char *ShortRules_handTypeNames[ShortRules_HandType_LAST + 1];
extern const POKEREVAL_EXPORT char *ShortRules_handTypeNamesPadded[ShortRules_HandType_LAST + 1];
extern POKEREVAL_EXPORT int ShortRules_nSigCards[ShortRules_HandType_LAST + 1];
extern POKEREVAL_EXPORT int ShortRules_HandVal_toString(HandVal handval, char *outString);
extern POKEREVAL_EXPORT int ShortRules_HandVal_print(HandVal handval);

#endif

#ifdef RULES_SHORT

#if defined(HandType_COUNT)
#include "rules_undef.h"
#endif

// Define shorthand macros for ShortDeck hand types and related functions
#define HandType_NOPAIR    ShortRules_HandType_NOPAIR
#define HandType_ONEPAIR   ShortRules_HandType_ONEPAIR
#define HandType_TWOPAIR   ShortRules_HandType_TWOPAIR
#define HandType_STRAIGHT  ShortRules_HandType_STRAIGHT
#define HandType_TRIPS     ShortRules_HandType_TRIPS
#define HandType_FULLHOUSE ShortRules_HandType_FULLHOUSE
#define HandType_FLUSH     ShortRules_HandType_FLUSH
#define HandType_QUADS     ShortRules_HandType_QUADS
#define HandType_STFLUSH   ShortRules_HandType_STFLUSH
#define HandType_FIRST     ShortRules_HandType_FIRST
#define HandType_COUNT     ShortRules_HandType_COUNT
#define HandType_LAST      ShortRules_HandType_LAST

#define handTypeNames        ShortRules_handTypeNames
#define handTypeNamesPadded  ShortRules_handTypeNamesPadded
#define nSigCards            ShortRules_nSigCards
#define HandVal_print        ShortRules_HandVal_print
#define HandVal_toString     ShortRules_HandVal_toString

#endif