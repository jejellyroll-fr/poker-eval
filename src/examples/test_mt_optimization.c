/*
 * test_mt_optimization.c - Simple test to validate MT optimization
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "poker_eval/utils/omp_compat.h"
#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>

// Generate pocket pairs
static int generatePairs(StdDeck_CardMask *hands, int rank) {
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
            StdDeck_CardMask_RESET(hands[count]);
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
            count++;
        }
    }
    return count;
}

int main(void) {
    printf("=== MULTITHREADING OPTIMIZATION TEST ===\n");
    printf("Max threads available: %d\n\n", omp_get_max_threads());
    
    // Simple test: AA vs KK
    StdDeck_CardMask range1_hands[6], range2_hands[6];
    
    int range1_count = generatePairs(range1_hands, StdDeck_Rank_ACE);
    int range2_count = generatePairs(range2_hands, StdDeck_Rank_KING);
    
    printf("Test: AA vs KK (preflop)\n");
    printf("Range 1: %d combos, Range 2: %d combos\n", range1_count, range2_count);
    printf("Total matchups: %d\n\n", range1_count * range2_count);
    
    PlayerRange ranges[2] = {
        {range1_hands, NULL, range1_count, (double)range1_count},
        {range2_hands, NULL, range2_count, (double)range2_count}
    };
    
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    enum_result_t result_st, result_mt, result_mt_v2;
    
    // Test 1: Single-threaded
    printf("1. Single-threaded...\n");
    enumResultAlloc(&result_st, 2, enum_ordering_mode_hi);
    clock_t start = clock();
    int matchups_st = CalculateEquityForRanges(
        game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result_st
    );
    double time_st = ((double)(clock() - start)) / CLOCKS_PER_SEC;
    printf("   Time: %.4f sec, Matchups: %d, EV: %.2f%% vs %.2f%%\n\n", 
           time_st, matchups_st, result_st.ev[0]*100, result_st.ev[1]*100);
    
    // Test 2: MT original (4 threads)
    printf("2. MT original (4 threads)...\n");
    enumResultAlloc(&result_mt, 2, enum_ordering_mode_hi);
    start = clock();
    int matchups_mt = CalculateEquityForRanges_MT(
        game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result_mt, 4
    );
    double time_mt = ((double)(clock() - start)) / CLOCKS_PER_SEC;
    printf("   Time: %.4f sec, Speedup: %.2fx, EV: %.2f%% vs %.2f%%\n\n", 
           time_mt, time_st/time_mt, result_mt.ev[0]*100, result_mt.ev[1]*100);
    
    // Test 3: MT v2 optimized (4 threads)
    printf("3. MT v2 optimized (4 threads)...\n");
    enumResultAlloc(&result_mt_v2, 2, enum_ordering_mode_hi);
    start = clock();
    int matchups_mt_v2 = CalculateEquityForRanges_MT_v2(
        game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result_mt_v2, 4
    );
    double time_mt_v2 = ((double)(clock() - start)) / CLOCKS_PER_SEC;
    printf("   Time: %.4f sec, Speedup: %.2fx, EV: %.2f%% vs %.2f%%\n", 
           time_mt_v2, time_st/time_mt_v2, result_mt_v2.ev[0]*100, result_mt_v2.ev[1]*100);
    printf("   Improvement over MT original: %.1f%%\n\n", 
           ((time_mt - time_mt_v2) / time_mt) * 100);
    
    // Verify results match
    if (matchups_st == matchups_mt && matchups_st == matchups_mt_v2) {
        printf("✓ All versions evaluated same number of matchups\n");
    } else {
        printf("✗ Matchup count mismatch!\n");
    }
    
    double ev_diff_mt = fabs(result_st.ev[0] - result_mt.ev[0]);
    double ev_diff_v2 = fabs(result_st.ev[0] - result_mt_v2.ev[0]);
    
    if (ev_diff_mt < 0.0001 && ev_diff_v2 < 0.0001) {
        printf("✓ All versions produced identical results\n");
    } else {
        printf("✗ Results don't match!\n");
    }
    
    enumResultFree(&result_st);
    enumResultFree(&result_mt);
    enumResultFree(&result_mt_v2);
    
    printf("\n=== TEST COMPLETE ===\n");
    return 0;
}
