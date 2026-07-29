/*
 * benchmark_large_ranges.c - Benchmark with large ranges for MT optimization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "poker_eval/utils/omp_compat.h"

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>

// Timer utility
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

// Generate all pocket pairs from minRank to maxRank
static int generatePocketPairs(StdDeck_CardMask *hands, int minRank, int maxRank) {
    int count = 0;
    for (int rank = minRank; rank <= maxRank; rank++) {
        for (int suit1 = 0; suit1 < 4; suit1++) {
            for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
                StdDeck_CardMask_RESET(hands[count]);
                StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
                StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
                count++;
            }
        }
    }
    return count;
}

// Generate all suited hands of specific ranks
static int generateSuitedHands(StdDeck_CardMask *hands, int rank1, int rank2) {
    int count = 0;
    for (int suit = 0; suit < 4; suit++) {
        StdDeck_CardMask_RESET(hands[count]);
        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank1, suit));
        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank2, suit));
        count++;
    }
    return count;
}

// Generate all offsuit hands of specific ranks
static int generateOffsuitHands(StdDeck_CardMask *hands, int rank1, int rank2) {
    int count = 0;
    for (int suit1 = 0; suit1 < 4; suit1++) {
        for (int suit2 = 0; suit2 < 4; suit2++) {
            if (suit1 == suit2) continue;
            StdDeck_CardMask_RESET(hands[count]);
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank1, suit1));
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank2, suit2));
            count++;
        }
    }
    return count;
}

// Generate broadway hands (T+)
static int generateBroadwayRange(StdDeck_CardMask *hands) {
    int count = 0;
    int broadway_ranks[] = {StdDeck_Rank_TEN, StdDeck_Rank_JACK, 
                           StdDeck_Rank_QUEEN, StdDeck_Rank_KING, StdDeck_Rank_ACE};
    
    // All broadway pairs
    for (int i = 0; i < 5; i++) {
        count += generatePocketPairs(&hands[count], broadway_ranks[i], broadway_ranks[i]);
    }
    
    // All broadway non-pairs
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            count += generateSuitedHands(&hands[count], broadway_ranks[i], broadway_ranks[j]);
            count += generateOffsuitHands(&hands[count], broadway_ranks[i], broadway_ranks[j]);
        }
    }
    
    return count;
}

// Generate a tight range (top 10%)
static int generateTightRange(StdDeck_CardMask *hands) {
    int count = 0;
    
    // Premium pairs: AA-TT
    count += generatePocketPairs(&hands[count], StdDeck_Rank_TEN, StdDeck_Rank_ACE);
    
    // Premium broadway: AK, AQ, AJ, KQ
    count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_KING);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_KING);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_QUEEN);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_QUEEN);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_JACK);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_KING, StdDeck_Rank_QUEEN);
    
    return count;
}

// Generate a loose range (top 30%)
static int generateLooseRange(StdDeck_CardMask *hands) {
    int count = 0;
    
    // All pairs 22+
    count += generatePocketPairs(&hands[count], StdDeck_Rank_2, StdDeck_Rank_ACE);
    
    // All Ax
    for (int rank = StdDeck_Rank_2; rank <= StdDeck_Rank_KING; rank++) {
        count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, rank);
        if (rank >= StdDeck_Rank_9) { // A9o+
            count += generateOffsuitHands(&hands[count], StdDeck_Rank_ACE, rank);
        }
    }
    
    // All Kx suited, K9o+
    for (int rank = StdDeck_Rank_2; rank <= StdDeck_Rank_QUEEN; rank++) {
        count += generateSuitedHands(&hands[count], StdDeck_Rank_KING, rank);
        if (rank >= StdDeck_Rank_9) {
            count += generateOffsuitHands(&hands[count], StdDeck_Rank_KING, rank);
        }
    }
    
    // Some Qx, Jx, Tx
    count += generateSuitedHands(&hands[count], StdDeck_Rank_QUEEN, StdDeck_Rank_JACK);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_QUEEN, StdDeck_Rank_JACK);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_QUEEN, StdDeck_Rank_TEN);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_JACK, StdDeck_Rank_TEN);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_JACK, StdDeck_Rank_TEN);
    
    return count;
}

static void run_benchmark(const char *name, PlayerRange *ranges, int num_threads[]) {
    printf("\n=== %s ===\n", name);
    printf("Range 1: %d hands, Range 2: %d hands\n", ranges[0].count, ranges[1].count);
    printf("Total potential matchups: %d\n\n", ranges[0].count * ranges[1].count);
    
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    enum_result_t result;
    double times[10];
    int thread_configs[] = {1, 2, 4, 8, 0}; // 0 = auto
    const char *labels[] = {"1 thread", "2 threads", "4 threads", "8 threads", "auto"};
    
    // Benchmark each configuration
    for (int i = 0; i < 5; i++) {
        enumResultAlloc(&result, 2, enum_ordering_mode_hi);
        
        double start = get_time();
        int matchups;
        
        if (thread_configs[i] == 1) {
            // Single-threaded
            matchups = CalculateEquityForRanges(
                game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result
            );
        } else {
            // Multi-threaded v2
            matchups = CalculateEquityForRanges_MT_v2(
                game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, thread_configs[i]
            );
        }
        
        times[i] = get_time() - start;
        
        printf("%-12s: %.3f sec, %d matchups, EV: %.2f%% vs %.2f%%", 
               labels[i], times[i], matchups, 
               result.ev[0] / matchups * 100, 
               result.ev[1] / matchups * 100);
        
        if (i > 0) {
            printf(", Speedup: %.2fx", times[0] / times[i]);
        }
        printf("\n");
        
        enumResultFree(&result);
    }
    
    // Also test original MT for comparison
    printf("\nOriginal MT (4 threads) for comparison:\n");
    enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    double start = get_time();
    CalculateEquityForRanges_MT(
        game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, 4
    );
    double time_mt_orig = get_time() - start;
    printf("MT original : %.3f sec, Speedup vs ST: %.2fx\n", time_mt_orig, times[0] / time_mt_orig);
    printf("MT v2 improvement: %.1f%%\n", ((time_mt_orig - times[2]) / time_mt_orig) * 100);
    enumResultFree(&result);
}

int main(void) {
    printf("==============================================\n");
    printf("LARGE RANGE BENCHMARK FOR MT OPTIMIZATION\n");
    printf("==============================================\n");
    printf("Max threads available: %d\n", omp_get_max_threads());
    
    // Allocate space for hands
    StdDeck_CardMask *hands1 = malloc(sizeof(StdDeck_CardMask) * 500);
    StdDeck_CardMask *hands2 = malloc(sizeof(StdDeck_CardMask) * 500);
    
    // Test 1: Medium size - Tight vs Loose range
    int count1 = generateTightRange(hands1);
    int count2 = generateLooseRange(hands2);
    PlayerRange test1_ranges[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_benchmark("Test 1: Tight vs Loose Range", test1_ranges, NULL);
    
    // Test 2: Large size - All pairs vs Broadway
    count1 = generatePocketPairs(hands1, StdDeck_Rank_2, StdDeck_Rank_ACE);
    count2 = generateBroadwayRange(hands2);
    PlayerRange test2_ranges[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_benchmark("Test 2: All Pairs vs Broadway", test2_ranges, NULL);
    
    // Test 3: Very large - Loose vs Loose
    count1 = generateLooseRange(hands1);
    count2 = generateLooseRange(hands2);
    PlayerRange test3_ranges[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_benchmark("Test 3: Loose vs Loose (Large)", test3_ranges, NULL);
    
    free(hands1);
    free(hands2);
    
    printf("\n==============================================\n");
    printf("BENCHMARK COMPLETE\n");
    printf("==============================================\n");
    
    return 0;
}
