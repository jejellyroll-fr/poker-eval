/* rules_fusion.c -- Rules for Fusion poker */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/eval_fusion.h>
#include <poker_eval/games/rules_fusion.h>

int Fusion_EvaluateHand(StdDeck_CardMask hole, StdDeck_CardMask board,
                        HandVal *hival, LowHandVal *loval)
{
    int nhole = 0, nboard = 0;
    int i;
    if (loval)
        *loval = LowHandVal_NOTHING;
    for (i = 0; i < StdDeck_N_CARDS; i++)
    {
        if (StdDeck_CardMask_CARD_IS_SET(hole, i))
            nhole++;
        if (StdDeck_CardMask_CARD_IS_SET(board, i))
            nboard++;
    }
    if (nboard == 5 && nhole != 4)
    {
        if (nhole == 2)
        {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, hole, board);
            *hival = StdDeck_StdRules_EVAL_N(combined, nhole + nboard);
            return 0;
        }
        else
            return 2;
    }
    int expected;
    switch (nboard)
    {
    case 0:
        expected = 2;
        break;
    case 3:
        expected = 3;
        break;
    case 4:
    case 5:
        expected = 4;
        break;
    default:
        return 1;
    }
    if (nhole != expected)
        return 2;
    if (nboard <= 3)
    {
        StdDeck_CardMask combined;
        StdDeck_CardMask_OR(combined, hole, board);
        *hival = StdDeck_StdRules_EVAL_N(combined, nhole + nboard);
        return 0;
    }
    return StdDeck_Fusion_EVAL(hole, board, hival);
}

int Fusion_MinPocket(void) { return 2; }
int Fusion_MaxPocket(void) { return 4; }
int Fusion_MinBoard(void) { return 3; }
int Fusion_MaxBoard(void) { return 5; }
int Fusion_RequiresJoker(void) { return 0; }
