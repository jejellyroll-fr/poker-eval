/*
 * rules_fusion.h -- Rules for Fusion poker
 *
 * Fusion poker combines Hold'em and Omaha mechanics
 */

#ifndef __RULES_FUSION_H__
#define __RULES_FUSION_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/handval_low.h>

/* Function prototypes for Fusion poker */
extern int Fusion_EvaluateHand(StdDeck_CardMask hole, StdDeck_CardMask board,
                               HandVal *hival, LowHandVal *loval);

extern int Fusion_MinPocket(void);
extern int Fusion_MaxPocket(void);
extern int Fusion_MinBoard(void);
extern int Fusion_MaxBoard(void);
extern int Fusion_RequiresJoker(void);

#endif
