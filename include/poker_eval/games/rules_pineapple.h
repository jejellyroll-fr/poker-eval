/*
 * Copyright (C) 2024
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

#ifndef __RULES_PINEAPPLE_H__
#define __RULES_PINEAPPLE_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/pokereval_export.h>
#include <poker_eval/deck/deck_std.h>

/* Pineapple Hold'em specific structures and functions */

typedef struct
{
    StdDeck_CardMask initial_hand; /* Original 3-card hand */
    StdDeck_CardMask current_hand; /* Hand after discard (2 cards) */
    int discarded;                 /* 1 if card discarded after flop */
} PineappleHandState;

/* Function to evaluate a Pineapple hand by finding the optimal discard */
extern POKEREVAL_EXPORT HandVal Pineapple_EVAL(StdDeck_CardMask pocket, StdDeck_CardMask board);

/* Function to find the best 2-card combination from 3 cards given a board */
extern POKEREVAL_EXPORT StdDeck_CardMask Pineapple_FindBestDiscard(StdDeck_CardMask pocket, StdDeck_CardMask board);

#endif /* __RULES_PINEAPPLE_H__ */
