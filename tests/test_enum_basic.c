/*
 * test_enum_basic.c - Basic test of enumExhaustive
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>

int main(int argc, char *argv[]) {
    enum_game_t game = game_holdem;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 3;
    int err;
    int exit_code = 0;
    
    printf("Testing basic enumExhaustive with holdem\n");
    printf("========================================\n\n");
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    
    // Player 1: As Kh
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    // Player 2: 7c 2d
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_DIAMONDS));
    
    // Flop: 3c 5d 9h
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_HEARTS));

    // Dead cards = all pocket cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("Player 1: As Kh\n");
    printf("Player 2: 7c 2d\n");
    printf("Dead cards count: %d\n", StdDeck_numCards(dead));
    
    printf("Board cards count: %d\n", StdDeck_numCards(board));
    printf("\nCalling enumExhaustive...\n");
    
    err = enumExhaustive(game, pockets, board, dead, npockets, nboard, 0, &result);
    
    if (err) {
        printf("ERROR: enumExhaustive returned %d\n", err);
        exit_code = 1;
    } else {
        printf("\nResults:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[0], result.nwinhi[0], result.ntiehi[0], result.nlosehi[0]);
        printf("Player 2 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[1], result.nwinhi[1], result.ntiehi[1], result.nlosehi[1]);
    }
    
    printf("\nCalling enumExhaustive_dispatch...\n");
    enum_result_t dispatch_result;
    enumResultClear(&dispatch_result);
    err = enumExhaustive_dispatch(game, pockets, board, dead, npockets, nboard, 0, &dispatch_result);
    if (err) {
        printf("ERROR: enumExhaustive_dispatch returned %d\n", err);
        exit_code = 1;
    } else {
        printf("Dispatch Samples: %u\n", dispatch_result.nsamples);
        printf("Dispatch Player 1 EV: %.4f\n", dispatch_result.ev[0]);
        printf("Dispatch Player 2 EV: %.4f\n", dispatch_result.ev[1]);
        if (dispatch_result.nsamples != result.nsamples ||
            dispatch_result.nwinhi[0] != result.nwinhi[0] ||
            dispatch_result.nlosehi[0] != result.nlosehi[0]) {
            printf("ERROR: dispatcher results differ from baseline\n");
            exit_code = 1;
        }
    }

    enumResultFree(&result);
    enumResultFree(&dispatch_result);
    
    return exit_code;
}
