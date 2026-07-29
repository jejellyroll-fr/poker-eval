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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/games/eval_pineapple.h>

int main(void) {
    StdDeck_CardMask pocket, board, dead;
    HandVal hand_val;
    enum_result_t result;
    int err;
    
    printf("Testing Pineapple Hold'em implementation...\n");
    
    /* Initialize card masks */
    StdDeck_CardMask_RESET(pocket);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Test 1: Basic Pineapple evaluation with 3 pocket cards */
    printf("\nTest 1: Basic Pineapple evaluation\n");
    
    /* Pocket: As Ah Kh (should discard Kh to keep pocket aces) */
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* Board: 2c 3d 4s 5h 6c */
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_CLUBS));
    
    hand_val = Pineapple_EVAL(pocket, board);
    printf("Hand value: %d\n", hand_val);
    
    if (hand_val == HandVal_NOTHING) {
        printf("ERROR: Pineapple evaluation returned HandVal_NOTHING\n");
        return 1;
    }
    
    printf("Hand type: %s\n", HandVal_toString(hand_val));
    
    /* Test 2: Enumeration test with 2 players */
    printf("\nTest 2: Pineapple enumeration test\n");
    
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    /* Player 1: As Ah Kh */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* Player 2: Ks Kd Qh */
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));
    
    /* Add these cards to dead cards */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    /* Run a small Monte Carlo simulation */
    err = enumSample(game_pineapple, pockets, board, dead, 2, 0, 1000, 0, &result);
    
    if (err != 0) {
        printf("ERROR: enumSample failed with error %d\n", err);
        return 1;
    }
    
    printf("Monte Carlo results (1000 iterations):\n");
    printf("Player 1 (AA): EV = %.3f\n", result.ev[0] / result.nsamples);
    printf("Player 2 (KK): EV = %.3f\n", result.ev[1] / result.nsamples);
    
    /* Player 1 should have higher equity with pocket aces */
    if (result.ev[0] <= result.ev[1]) {
        printf("WARNING: Expected player 1 (AA) to have higher equity than player 2 (KK)\n");
    }
    
    printf("\nPineapple Hold'em implementation test completed successfully!\n");
    
    return 0;
}
