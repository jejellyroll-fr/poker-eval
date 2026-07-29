#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

int main(void) {
    printf("=== SIMPLE EQUITY TEST ===\n");
    
    // Create specific hands: AsAh vs KsKh
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    
    // AsAh
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    
    // KsKh
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    // Print hands
    printf("Player 1: ");
    StdDeck_printMask(pockets[0]);
    printf("\nPlayer 2: ");
    StdDeck_printMask(pockets[1]);
    printf("\n");
    
    // Empty board and dead cards
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    // Calculate equity
    enum_result_t result;
    if (enumResultAlloc(&result, 2, enum_ordering_mode_hi) != 0) {
        printf("Failed to allocate result\n");
        return 1;
    }
    
    int iterations = 50000;
    printf("\nCalculating equity (Monte Carlo, %d iterations)...\n", iterations);
    // npockets=2, nboard=0 (no board cards dealt yet), orderflag=0
    int ret = enumSample(game_holdem, pockets, board, dead, 2, 0, iterations, 0, &result);
    
    if (ret == 0) {
        printf("\nResults:\n");
        printf("Total samples: %u\n", result.nsamples);
        printf("Player 1 equity: %.2f%%\n", (result.ev[0] / result.nsamples) * 100);
        printf("Player 2 equity: %.2f%%\n", (result.ev[1] / result.nsamples) * 100);
        printf("Player 1 wins: %u (%.2f%%)\n", result.nwinhi[0], 
               (double)result.nwinhi[0] / result.nsamples * 100);
        printf("Player 2 wins: %u (%.2f%%)\n", result.nwinhi[1],
               (double)result.nwinhi[1] / result.nsamples * 100);
        printf("Ties: %u (%.2f%%)\n", result.ntiehi[0],
               (double)result.ntiehi[0] / result.nsamples * 100);
    } else {
        printf("Enumeration failed with code: %d\n", ret);
    }
    
    enumResultFree(&result);
    
    return 0;
}
