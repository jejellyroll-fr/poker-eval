/*
 * Copyright (C) 2024
 *           poker-eval development team
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

#ifndef EVAL_DRAWMAHA_H
#define EVAL_DRAWMAHA_H

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/core/handval_low.h>
#include <poker_eval/games/rules_drawmaha.h>
#include <poker_eval/games/eval_omaha.h>

/* Inline evaluation functions for Drawmaha */

/* Fast evaluation assuming optimal draw */
static inline int
StdDeck_DrawmahaRules_EVAL_OPTIMAL(StdDeck_CardMask hand,
                                   StdDeck_CardMask board,
                                   HandVal *hival,
                                   LowHandVal *loval)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    return Drawmaha_EvaluateOptimal(hand, board, dead, hival, loval);
}

/* Evaluation with specific draw mask */
static inline int
StdDeck_DrawmahaRules_EVAL_WITH_DRAW(StdDeck_CardMask hand,
                                     StdDeck_CardMask board,
                                     int draw_mask,
                                     StdDeck_CardMask replacement_cards,
                                     HandVal *hival,
                                     LowHandVal *loval)
{
    DrawmahaDrawState state;
    StdDeck_CardMask final_hand;

    /* Initialize draw state */
    if (Drawmaha_InitDrawState(&state, hand) != 0)
    {
        return 1;
    }

    /* Execute the draw */
    if (Drawmaha_ExecuteDraw(&state, draw_mask, replacement_cards) != 0)
    {
        return 1;
    }

    final_hand = state.current_hand;

    /* Evaluate using Omaha rules */
    return Drawmaha_EvaluateHand(final_hand, board, hival, loval);
}

/* Simple evaluation for enumeration (assumes optimal play) */
static inline int
StdDeck_DrawmahaRules_EVAL_N(StdDeck_CardMask hand,
                             StdDeck_CardMask board,
                             HandVal *hival)
{
    LowHandVal loval;
    return StdDeck_DrawmahaRules_EVAL_OPTIMAL(hand, board, hival, &loval);
}

/* Hi/Lo evaluation for split pot games */
static inline int
StdDeck_DrawmahaRules_EVAL_HILO(StdDeck_CardMask hand,
                                StdDeck_CardMask board,
                                HandVal *hival,
                                LowHandVal *loval)
{
    return StdDeck_DrawmahaRules_EVAL_OPTIMAL(hand, board, hival, loval);
}

/* Macro for compatibility with existing enumeration system */
#define StdDeck_DrawmahaRules_EVAL(hand, board, hival) \
    StdDeck_DrawmahaRules_EVAL_N(hand, board, hival)

#endif /* EVAL_DRAWMAHA_H */
