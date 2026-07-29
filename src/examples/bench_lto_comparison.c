/* bench_lto_comparison.c - Demonstrate LTO optimization benefits */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

/* Timer function */
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/* Intensive computation that benefits from LTO */
static void bench_cross_module_calls(int iterations) {
    printf("\n=== Cross-Module Function Call Benchmark ===\n");
    printf("Testing %d iterations of mixed operations...\n", iterations);
    
    StdDeck_CardMask hand, dead;
    uint64_t sum = 0;
    
    /* Initialize random number generator */
    srand(42);
    
    double start = get_time();
    
    for (int i = 0; i < iterations; i++) {
        /* Reset masks */
        StdDeck_CardMask_RESET(hand);
        StdDeck_CardMask_RESET(dead);
        
        /* Add random cards */
        for (int j = 0; j < 7; j++) {
            int card = rand() % 52;
            StdDeck_CardMask_SET(hand, card);
        }
        
        /* Multiple cross-module calls that LTO can optimize */
        int val = StdDeck_StdRules_EVAL_N(hand, 7);
        sum += val;
        
        /* Card operations from different modules */
        int n_cards = StdDeck_numCards(hand);
        sum += n_cards;
        
        /* Mask operations */
        StdDeck_CardMask temp;
        StdDeck_CardMask_OR(temp, hand, dead);
        sum += StdDeck_CardMask_ANY_SET(temp, dead);
    }
    
    double elapsed = get_time() - start;
    
    printf("Time: %.3f ms\n", elapsed * 1000);
    printf("Operations/sec: %.0f\n", iterations / elapsed);
    printf("Checksum: %" PRIu64 "\n", sum);
}

/* Benchmark inline expansion opportunities */
static void bench_inline_expansion(int iterations) {
    printf("\n=== Inline Expansion Benchmark ===\n");
    printf("Testing small function inlining across modules...\n");
    
    uint64_t sum = 0;
    int cards[52];
    
    /* Initialize cards */
    for (int i = 0; i < 52; i++) {
        cards[i] = i;
    }
    
    double start = get_time();
    
    for (int i = 0; i < iterations; i++) {
        /* Shuffle first 7 cards */
        for (int j = 0; j < 7; j++) {
            int k = j + (rand() % (52 - j));
            int temp = cards[j];
            cards[j] = cards[k];
            cards[k] = temp;
        }
        
        /* Many small operations that benefit from inlining */
        for (int j = 0; j < 7; j++) {
            int rank = StdDeck_RANK(cards[j]);
            int suit = StdDeck_SUIT(cards[j]);
            sum += rank * 4 + suit;
            
            /* More cross-module calls */
            sum += StdDeck_Rank_COUNT;
            sum += StdDeck_Suit_COUNT;
        }
    }
    
    double elapsed = get_time() - start;
    
    printf("Time: %.3f ms\n", elapsed * 1000);
    printf("Operations/sec: %.0f\n", (iterations * 7 * 5) / elapsed);
    printf("Checksum: %" PRIu64 "\n", sum);
}

/* Benchmark constant propagation across modules */
static void bench_constant_propagation(int iterations) {
    printf("\n=== Constant Propagation Benchmark ===\n");
    printf("Testing cross-module constant optimization...\n");
    
    uint64_t sum = 0;
    double start = get_time();
    
    for (int i = 0; i < iterations; i++) {
        /* Operations with constants from different modules */
        sum += StdDeck_N_CARDS * StdDeck_Rank_COUNT;
        sum += StdDeck_Suit_FIRST * StdDeck_Suit_COUNT;
        
        /* Conditional compilation constants */
        #ifdef CARDS_DEALT
        sum += CARDS_DEALT;
        #else
        sum += 5;
        #endif
        
        /* Enum constants */
        sum += game_holdem;
        sum += ENUM_EXHAUSTIVE;
    }
    
    double elapsed = get_time() - start;
    
    printf("Time: %.3f ms\n", elapsed * 1000);
    printf("Operations/sec: %.0f\n", iterations / elapsed);
    printf("Checksum: %" PRIu64 "\n", sum);
}

/* Main benchmark */
int main(int argc, char *argv[]) {
    printf("=== Link Time Optimization (LTO) Benchmark ===\n");
    printf("\nThis benchmark demonstrates operations that benefit from LTO:\n");
    printf("- Cross-module function inlining\n");
    printf("- Constant propagation across translation units\n");
    printf("- Dead code elimination\n");
    printf("- Whole program optimization\n");
    
    int iterations = 10000000;
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }
    
    /* Run benchmarks */
    bench_cross_module_calls(iterations / 10);  // Fewer iterations as this is slower
    bench_inline_expansion(iterations);
    bench_constant_propagation(iterations);
    
    printf("\n=== LTO Benefits ===\n");
    printf("With LTO enabled, the compiler can:\n");
    printf("1. Inline functions across module boundaries\n");
    printf("2. Eliminate unused code paths\n");
    printf("3. Optimize based on whole-program analysis\n");
    printf("4. Propagate constants across modules\n");
    printf("\nExpected improvement: 5-8%% overall performance gain\n");
    
    return 0;
}
