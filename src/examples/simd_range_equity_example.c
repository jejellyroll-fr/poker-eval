/*
 * simd_range_equity_example.c: Example of using SIMD operations for range equity calculation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/equity/simd_operations.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/eval.h>

/* Example: Calculate equity for pocket pairs */
static void example_pocket_pairs(void) {
    StdDeck_CardMask aces[6], kings[6];
    HandVal ace_values[6], king_values[6];
    int i, j, idx;
    
    printf("=== Pocket Pairs Example (AA vs KK) ===\n");
    
    /* Generate all AA combinations */
    idx = 0;
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 4; j++) {
            StdDeck_CardMask_RESET(aces[idx]);
            StdDeck_CardMask_SET(aces[idx], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, i));
            StdDeck_CardMask_SET(aces[idx], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, j));
            idx++;
        }
    }
    
    /* Generate all KK combinations */
    idx = 0;
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 4; j++) {
            StdDeck_CardMask_RESET(kings[idx]);
            StdDeck_CardMask_SET(kings[idx], StdDeck_MAKE_CARD(StdDeck_Rank_KING, i));
            StdDeck_CardMask_SET(kings[idx], StdDeck_MAKE_CARD(StdDeck_Rank_KING, j));
            idx++;
        }
    }
    
    /* Time the calculation */
    clock_t start = clock();
    
    /* Evaluate all AA hands */
    simd_eval_multiple_hands(aces, 6, ace_values);
    
    /* Evaluate all KK hands */
    simd_eval_multiple_hands(kings, 6, king_values);
    
    clock_t end = clock();
    
    printf("Evaluated %d hands in %.3f ms using SIMD\n", 
           12, ((double)(end - start)) / CLOCKS_PER_SEC * 1000);
    printf("\n");
}

/* Example: Batch evaluation of multiple scenarios */
static void example_batch_scenarios(void) {
    printf("=== Batch Evaluation Example ===\n");
    
    /* Simulate evaluating multiple tables simultaneously */
    const int num_tables = 8;
    StdDeck_CardMask tables[8][2]; /* 2 players per table */
    StdDeck_CardMask boards[8];
    StdDeck_CardMask final_hands[16]; /* All hands to evaluate */
    HandVal results[16];
    int i, j;
    
    /* Generate random scenarios */
    for (i = 0; i < num_tables; i++) {
        StdDeck_CardMask used;
        StdDeck_CardMask_RESET(used);
        
        /* Deal hole cards to 2 players */
        for (j = 0; j < 2; j++) {
            StdDeck_CardMask_RESET(tables[i][j]);
            
            /* Deal 2 cards */
            int card1, card2;
            do {
                card1 = rand() % 52;
            } while (StdDeck_CardMask_CARD_IS_SET(used, card1));
            StdDeck_CardMask_SET(tables[i][j], card1);
            StdDeck_CardMask_SET(used, card1);
            
            do {
                card2 = rand() % 52;
            } while (StdDeck_CardMask_CARD_IS_SET(used, card2));
            StdDeck_CardMask_SET(tables[i][j], card2);
            StdDeck_CardMask_SET(used, card2);
        }
        
        /* Deal 5 board cards */
        StdDeck_CardMask_RESET(boards[i]);
        for (j = 0; j < 5; j++) {
            int card;
            do {
                card = rand() % 52;
            } while (StdDeck_CardMask_CARD_IS_SET(used, card));
            StdDeck_CardMask_SET(boards[i], card);
            StdDeck_CardMask_SET(used, card);
        }
        
        /* Combine hole cards with board */
        for (j = 0; j < 2; j++) {
            final_hands[i * 2 + j] = tables[i][j];
            StdDeck_CardMask_OR(final_hands[i * 2 + j], final_hands[i * 2 + j], boards[i]);
        }
    }
    
    /* Evaluate all hands using SIMD */
    clock_t start = clock();
    simd_eval_multiple_hands(final_hands, 16, results);
    clock_t end = clock();
    
    /* Display results */
    printf("Evaluated %d tables (%d hands) in %.3f ms\n",
           num_tables, num_tables * 2,
           ((double)(end - start)) / CLOCKS_PER_SEC * 1000);
    
    /* Show winners for each table */
    for (i = 0; i < num_tables; i++) {
        HandVal p1_val = results[i * 2];
        HandVal p2_val = results[i * 2 + 1];
        
        printf("Table %d: ", i + 1);
        if (p1_val > p2_val) {
            printf("Player 1 wins\n");
        } else if (p2_val > p1_val) {
            printf("Player 2 wins\n");
        } else {
            printf("Tie\n");
        }
    }
    
    printf("\n");
}

/* Example: Performance comparison */
static void example_performance_comparison(void) {
    const int num_hands = 1000;
    StdDeck_CardMask hands[1000];
    HandVal scalar_results[1000];
    HandVal simd_results[1000];
    int i, j;
    clock_t start, end;
    double scalar_time, simd_time;
    
    printf("=== Performance Comparison ===\n");
    printf("Evaluating %d random 7-card hands...\n", num_hands);
    
    /* Generate random hands */
    for (i = 0; i < num_hands; i++) {
        StdDeck_CardMask_RESET(hands[i]);
        StdDeck_CardMask used;
        StdDeck_CardMask_RESET(used);
        
        /* Add 7 random cards */
        for (j = 0; j < 7; j++) {
            int card;
            do {
                card = rand() % 52;
            } while (StdDeck_CardMask_CARD_IS_SET(used, card));
            StdDeck_CardMask_SET(hands[i], card);
            StdDeck_CardMask_SET(used, card);
        }
    }
    
    /* Scalar evaluation */
    start = clock();
    for (i = 0; i < num_hands; i++) {
        scalar_results[i] = StdDeck_StdRules_EVAL_N(hands[i], 7);
    }
    end = clock();
    scalar_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    /* SIMD evaluation */
    start = clock();
    simd_eval_multiple_hands(hands, num_hands, simd_results);
    end = clock();
    simd_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    /* Verify results match */
    int errors = 0;
    for (i = 0; i < num_hands; i++) {
        if (scalar_results[i] != simd_results[i]) {
            errors++;
        }
    }
    
    printf("\nResults:\n");
    printf("  Scalar evaluation: %.3f ms\n", scalar_time * 1000);
    printf("  SIMD evaluation: %.3f ms\n", simd_time * 1000);
    printf("  Speedup: %.2fx\n", scalar_time / simd_time);
    printf("  Errors: %d\n", errors);
    
    /* Show SIMD capability being used */
    simd_capability_t cap = simd_detect_capability();
    printf("  Using: %s\n", simd_capability_name(cap));
    
    printf("\n");
}

/* Example: Range vs Range with SIMD */
static void example_range_vs_range(void) {
    printf("=== Range vs Range Example ===\n");
    
    /* Create two small ranges */
    StdDeck_CardMask range1[10], range2[10];
    HandVal results1[10], results2[10];
    int i;
    
    /* Range 1: High pairs (AA, KK, QQ) */
    int range1_size = 0;
    int ranks[] = {StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN};
    
    for (int r = 0; r < 3; r++) {
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                if (range1_size < 10) {
                    StdDeck_CardMask_RESET(range1[range1_size]);
                    StdDeck_CardMask_SET(range1[range1_size], StdDeck_MAKE_CARD(ranks[r], s1));
                    StdDeck_CardMask_SET(range1[range1_size], StdDeck_MAKE_CARD(ranks[r], s2));
                    range1_size++;
                }
            }
        }
    }
    
    /* Range 2: Medium pairs (JJ, TT, 99) */
    int range2_size = 0;
    int ranks2[] = {StdDeck_Rank_JACK, StdDeck_Rank_TEN, StdDeck_Rank_9};
    
    for (int r = 0; r < 3; r++) {
        for (int s1 = 0; s1 < 4; s1++) {
            for (int s2 = s1 + 1; s2 < 4; s2++) {
                if (range2_size < 10) {
                    StdDeck_CardMask_RESET(range2[range2_size]);
                    StdDeck_CardMask_SET(range2[range2_size], StdDeck_MAKE_CARD(ranks2[r], s1));
                    StdDeck_CardMask_SET(range2[range2_size], StdDeck_MAKE_CARD(ranks2[r], s2));
                    range2_size++;
                }
            }
        }
    }
    
    /* Evaluate both ranges using SIMD */
    clock_t start = clock();
    simd_eval_multiple_hands(range1, range1_size, results1);
    simd_eval_multiple_hands(range2, range2_size, results2);
    clock_t end = clock();
    
    printf("Evaluated %d hands in %.3f ms\n",
           range1_size + range2_size,
           ((double)(end - start)) / CLOCKS_PER_SEC * 1000);
    
    /* Show average hand strength */
    long long avg1 = 0, avg2 = 0;
    for (i = 0; i < range1_size; i++) avg1 += results1[i];
    for (i = 0; i < range2_size; i++) avg2 += results2[i];
    
    printf("Range 1 (high pairs) average: %lld\n", avg1 / range1_size);
    printf("Range 2 (medium pairs) average: %lld\n", avg2 / range2_size);
    
    printf("\n");
}

int main(int argc, char* argv[]) {
    /* Initialize random seed */
    srand((unsigned int)time(NULL));
    
    printf("SIMD Range Equity Examples\n");
    printf("==========================\n\n");
    
    /* Show detected SIMD capability */
    simd_capability_t cap = simd_detect_capability();
    printf("Detected SIMD capability: %s\n\n", simd_capability_name(cap));
    
    /* Run examples */
    example_pocket_pairs();
    example_batch_scenarios();
    example_performance_comparison();
    example_range_vs_range();
    
    printf("Examples completed.\n");
    
    return 0;
}
