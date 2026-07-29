/* simple_lto_demo.c - Simple demonstration of LTO benefits */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include <poker_eval/poker_eval.h>

/* Timer function */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

int main(void) {
    printf("=== Simple LTO Performance Test ===\n\n");
    
    const int iterations = 50000000;
    StdDeck_CardMask hand;
    uint64_t sum = 0;
    
    printf("Evaluating %d random 7-card hands...\n", iterations);
    
    /* Initialize random seed */
    srand(42);
    
    /* Start timing */
    double start = get_time();
    
    /* Main benchmark loop */
    for (int i = 0; i < iterations; i++) {
        /* Create random hand */
        StdDeck_CardMask_RESET(hand);
        
        /* Add 7 random cards */
        for (int j = 0; j < 7; j++) {
            int card = (rand() % 52);
            /* Avoid duplicates in a simple way */
            while (StdDeck_CardMask_CARD_IS_SET(hand, card)) {
                card = (card + 1) % 52;
            }
            StdDeck_CardMask_SET(hand, card);
        }
        
        /* Evaluate hand */
        HandVal val = StdDeck_StdRules_EVAL_N(hand, 7);
        sum += HandVal_HANDTYPE(val);
    }
    
    double elapsed = get_time() - start;
    
    /* Results */
    printf("\nResults:\n");
    printf("Time: %.3f seconds\n", elapsed);
    printf("Hands/second: %.0f\n", iterations / elapsed);
    printf("Checksum: %" PRIu64 "\n", sum);
    
    printf("\nWith LTO enabled, this benchmark benefits from:\n");
    printf("- Inlined card manipulation macros\n");
    printf("- Optimized evaluation function calls\n");
    printf("- Better register allocation across modules\n");
    
    return 0;
}
