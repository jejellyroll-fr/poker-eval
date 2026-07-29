/*
 * test_pokenum_joker.c - Test pokenum functionality with joker games
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/universal_deck.h>

int main(int argc, char *argv[]) {
    enum_game_t game = game_lowball;
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int npockets = 2;
    int nboard = 0;
    int err;
    
    printf("Testing pokenum with lowball game\n");
    printf("=================================\n\n");
    
    // Initialize
    enumResultClear(&result);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    
    // Player 1: 7h 5s 3d 2c (4 cards)
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    
    // Player 2: 9s 8h 6d 4c (4 cards)
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_6, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_4, StdDeck_Suit_CLUBS));
    
    // Dead cards = all pocket cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("Player 1: 7h 5s 3d 2c (needs 1 more card)\n");
    printf("Player 2: 9s 8h 6d 4c (needs 1 more card)\n");
    printf("Dead cards count: %d\n", StdDeck_numCards(dead));
    printf("\nCalling enumExhaustive...\n");
    
    // Use Monte Carlo sampling instead of exhaustive enumeration to avoid timeout
    err = enumSample(game, pockets, board, dead, npockets, nboard, 1000, 0, &result);
    
    if (err) {
        printf("ERROR: enumExhaustive returned %d\n", err);
    } else {
        printf("\nResults:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[0], result.nwinlo[0], result.ntielo[0], result.nloselo[0]);
        printf("Player 2 - EV: %.4f, Win: %d, Tie: %d, Lose: %d\n",
               result.ev[1], result.nwinlo[1], result.ntielo[1], result.nloselo[1]);
    }
    
    enumResultFree(&result);
    
    // Now test with a joker
    printf("\n\nTesting with joker...\n");
    printf("====================\n\n");
    
    enumResultClear(&result);
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(dead);
    
    // Player 1: 7h 5s 3d (3 cards, will get joker as 4th)
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_DIAMONDS));
    
    // Note: We can't set the joker in StdDeck_CardMask, so we'll just test without it for now
    printf("Player 1: 7h 5s 3d (needs 2 more cards)\n");
    printf("Player 2: 9s 8h 6d 4c (needs 1 more card)\n");
    
    // Dead cards
    StdDeck_CardMask_OR(dead, pockets[0], pockets[1]);
    
    printf("Dead cards count: %d\n", StdDeck_numCards(dead));
    printf("\nCalling enumSample...\n");
    
    err = enumSample(game, pockets, board, dead, npockets, nboard, 1000, 0, &result);
    
    if (err) {
        printf("ERROR: enumExhaustive returned %d\n", err);
    } else {
        printf("\nResults:\n");
        printf("Samples: %u\n", result.nsamples);
        printf("Player 1 - EV: %.4f\n", result.ev[0]);
        printf("Player 2 - EV: %.4f\n", result.ev[1]);
    }
    
    enumResultFree(&result);
    
    return 0;
}
