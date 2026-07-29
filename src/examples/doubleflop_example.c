/* doubleflop_example.c - Example of double flop holdem usage */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/enumerate_simd.h>

/* Print a card mask */
static void print_hand(const char* label, StdDeck_CardMask hand) {
    printf("%s: ", label);
    StdDeck_printMask(hand);
    printf("\n");
}

/* Example 1: Basic double flop evaluation */
static void example_basic_doubleflop(void) {
    printf("\n=== Example 1: Basic Double Flop Evaluation ===\n");
    
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    
    /* Player 1: As Ah */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    
    /* Player 2: Ks Kh */
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    
    /* No board cards yet */
    StdDeck_CardMask_RESET(board);
    
    /* Dead cards = pocket cards */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);
    
    print_hand("Player 1", pockets[0]);
    print_hand("Player 2", pockets[1]);
    
    /* Run Monte Carlo simulation */
    printf("\nRunning Monte Carlo simulation (10,000 iterations)...\n");
    int err = enumSample(game_doubleflop_holdem, pockets, board, dead,
                        2, 0, 10000, 0, &result);
    
    if (err) {
        printf("Error: %d\n", err);
        return;
    }
    
    /* Print results */
    printf("\nResults:\n");
    printf("Player 1 (AA) EV: %.3f\n", result.ev[0] / result.nsamples);
    printf("Player 2 (KK) EV: %.3f\n", result.ev[1] / result.nsamples);
    printf("Player 1 scoops: %d (%.1f%%)\n", result.nscoop[0], 
           100.0 * result.nscoop[0] / result.nsamples);
    printf("Player 2 scoops: %d (%.1f%%)\n", result.nscoop[1],
           100.0 * result.nscoop[1] / result.nsamples);
}

/* Example 2: SIMD acceleration check */
static void example_simd_check(void) {
    printf("\n=== Example 2: SIMD Acceleration Check ===\n");
    
    if (enumerate_simd_available()) {
        printf("SIMD acceleration is AVAILABLE\n");
        printf("Info: %s\n", enumerate_simd_info());
    } else {
        printf("SIMD acceleration is NOT AVAILABLE\n");
        printf("Info: %s\n", enumerate_simd_info());
    }
}

/* Example 3: Multi-player double flop */
static void example_multiplayer_doubleflop(void) {
    printf("\n=== Example 3: Multi-Player Double Flop ===\n");
    
    StdDeck_CardMask pockets[6];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    
    /* Setup 6 players with premium hands */
    const char* hands[] = {"AA", "KK", "QQ", "JJ", "TT", "99"};
    int ranks[] = {StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN,
                   StdDeck_Rank_JACK, StdDeck_Rank_TEN, StdDeck_Rank_9};
    
    StdDeck_CardMask_RESET(dead);
    
    for (int i = 0; i < 6; i++) {
        StdDeck_CardMask_RESET(pockets[i]);
        StdDeck_CardMask_SET(pockets[i], StdDeck_MAKE_CARD(ranks[i], StdDeck_Suit_SPADES));
        StdDeck_CardMask_SET(pockets[i], StdDeck_MAKE_CARD(ranks[i], StdDeck_Suit_HEARTS));
        StdDeck_CardMask_OR(dead, dead, pockets[i]);
        printf("Player %d: %s\n", i+1, hands[i]);
    }
    
    StdDeck_CardMask_RESET(board);
    
    /* Run simulation */
    printf("\nRunning simulation (5,000 iterations)...\n");
    int err = enumSample(game_doubleflop_holdem, pockets, board, dead,
                        6, 0, 5000, 0, &result);
    
    if (err) {
        printf("Error: %d\n", err);
        return;
    }
    
    /* Print results */
    printf("\nResults (EV and scoop %%):\n");
    for (int i = 0; i < 6; i++) {
        printf("Player %d (%s): EV=%.3f, Scoops=%.1f%%\n", 
               i+1, hands[i],
               result.ev[i] / result.nsamples,
               100.0 * result.nscoop[i] / result.nsamples);
    }
}

/* Example 4: Performance comparison */
static void example_performance_comparison(void) {
    printf("\n=== Example 4: Performance Analysis ===\n");
    
    printf("Double Flop Holdem characteristics:\n");
    printf("- Evaluates 2 independent 5-card boards\n");
    printf("- Each player's hand is evaluated against both boards\n");
    printf("- Pot is split between winners of each board\n");
    printf("- 'Scoop' means winning both boards\n");
    
    printf("\nPerformance considerations:\n");
    printf("- Standard: Evaluates boards sequentially\n");
    printf("- SIMD: Evaluates both boards in parallel\n");
    printf("- Expected speedup with SIMD: ~20%%\n");
    
    printf("\nComplexity analysis:\n");
    printf("- 10 community cards total (2 x 5-card boards)\n");
    printf("- C(48,10) = 6,540,715,896 possible board combinations\n");
    printf("- Monte Carlo sampling essential for real-time analysis\n");
}

int main(void) {
    printf("=== Double Flop Holdem Examples ===\n");
    
    /* Check SIMD support first */
    example_simd_check();
    
    /* Run examples */
    example_basic_doubleflop();
    example_multiplayer_doubleflop();
    example_performance_comparison();
    
    printf("\n=== Summary ===\n");
    printf("Double Flop Holdem is a variant where:\n");
    printf("- Two separate 5-card boards are dealt\n");
    printf("- Players compete for half the pot on each board\n");
    printf("- SIMD optimization can improve performance by ~20%%\n");
    
    return 0;
}
