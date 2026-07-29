/*
 * benchmark_all_versions.c - Comprehensive benchmark of all MT versions
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

// Generate pocket pairs
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

// Generate suited hands
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

// Generate offsuit hands
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

// Generate a medium range
static int generateMediumRange(StdDeck_CardMask *hands) {
    int count = 0;
    
    // Pairs: 88+
    count += generatePocketPairs(&hands[count], StdDeck_Rank_8, StdDeck_Rank_ACE);
    
    // Broadway: AK, AQ, KQ
    count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_KING);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_KING);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_QUEEN);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_ACE, StdDeck_Rank_QUEEN);
    count += generateSuitedHands(&hands[count], StdDeck_Rank_KING, StdDeck_Rank_QUEEN);
    count += generateOffsuitHands(&hands[count], StdDeck_Rank_KING, StdDeck_Rank_QUEEN);
    
    return count;
}

typedef struct {
    const char *name;
    double time;
    int matchups;
    double ev[2];
    double speedup;
} BenchmarkResult;

static void run_comprehensive_benchmark(const char *test_name, PlayerRange *ranges) {
    printf("\n=== %s ===\n", test_name);
    printf("Range sizes: %d vs %d (potential: %d matchups)\n\n", 
           ranges[0].count, ranges[1].count, ranges[0].count * ranges[1].count);
    
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    BenchmarkResult results[11];
    int num_results = 0;
    
    // Test configurations
    struct {
        const char *name;
        int version; // 0=ST, 1=MT, 2=MT_v2, 3=MT_v3, 4=MT_v5
        int threads;
    } configs[] = {
        {"Single-threaded", 0, 1},
        {"MT original (2t)", 1, 2},
        {"MT original (4t)", 1, 4},
        {"MT v2 (2t)", 2, 2},
        {"MT v2 (4t)", 2, 4},
        {"MT v3 lazy (2t)", 3, 2},
        {"MT v3 lazy (4t)", 3, 4},
        {"MT v3 lazy (auto)", 3, 0},
        {"MT v5 ws (2t)", 4, 2},
        {"MT v5 ws (4t)", 4, 4},
        {"MT v5 ws (auto)", 4, 0}
    };
    
    double baseline_time = 0;
    int num_configs = sizeof(configs) / sizeof(configs[0]);
    
    for (int i = 0; i < num_configs; i++) {
        enum_result_t result;
        enumResultAlloc(&result, 2, enum_ordering_mode_hi);
        
        printf("%-20s: ", configs[i].name);
        fflush(stdout);
        
        double start = get_time();
        int matchups = 0;
        
        switch (configs[i].version) {
            case 0: // Single-threaded
                matchups = CalculateEquityForRanges(
                    game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result
                );
                break;
            case 1: // MT original
                matchups = CalculateEquityForRanges_MT(
                    game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, configs[i].threads
                );
                break;
            case 2: // MT v2
                matchups = CalculateEquityForRanges_MT_v2(
                    game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, configs[i].threads
                );
                break;
            case 3: // MT v3
                matchups = CalculateEquityForRanges_MT_v3(
                    game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, configs[i].threads
                );
                break;
            case 4: // MT v5 work-stealing
                matchups = CalculateEquityForRanges_MT_v5(
                    game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, configs[i].threads
                );
                break;
            default:
                matchups = 0;
                break;
        }
        
        double elapsed = get_time() - start;
        
        if (i == 0) baseline_time = elapsed;
        
        results[num_results].name = configs[i].name;
        results[num_results].time = elapsed;
        results[num_results].matchups = matchups;
        results[num_results].ev[0] = matchups > 0 ? result.ev[0] / matchups * 100 : 0;
        results[num_results].ev[1] = matchups > 0 ? result.ev[1] / matchups * 100 : 0;
        results[num_results].speedup = baseline_time / elapsed;
        
        printf("%.3fs, %d matchups, %.2f%% vs %.2f%%, speedup: %.2fx\n",
               elapsed, matchups, results[num_results].ev[0], results[num_results].ev[1],
               results[num_results].speedup);
        
        enumResultFree(&result);
        num_results++;
    }
    
    // Summary
    printf("\nPerformance Summary:\n");
    printf("%-20s %8s %8s %10s\n", "Version", "Time", "Speedup", "Efficiency");
    printf("%-20s %8s %8s %10s\n", "-------", "----", "-------", "----------");
    
    for (int i = 0; i < num_results; i++) {
        int threads = 1;
        if (strstr(results[i].name, "(2t)")) threads = 2;
        else if (strstr(results[i].name, "(4t)")) threads = 4;
        else if (strstr(results[i].name, "(auto)")) threads = omp_get_max_threads();
        
        double efficiency = (results[i].speedup / threads) * 100;
        
        printf("%-20s %7.3fs %7.2fx %9.1f%%\n",
               results[i].name, results[i].time, results[i].speedup, efficiency);
    }
    
    // Verify all versions produce same results
    int all_match = 1;
    for (int i = 1; i < num_results; i++) {
        if (fabs(results[0].ev[0] - results[i].ev[0]) > 0.01 ||
            fabs(results[0].ev[1] - results[i].ev[1]) > 0.01) {
            all_match = 0;
            break;
        }
    }
    
    printf("\nResult verification: %s\n", all_match ? "✓ All versions match" : "✗ Results differ!");
}

int main(void) {
    printf("==============================================\n");
    printf("COMPREHENSIVE MT OPTIMIZATION BENCHMARK\n");
    printf("==============================================\n");
    printf("System: %d threads available\n", omp_get_max_threads());
    
    StdDeck_CardMask *hands1 = malloc(sizeof(StdDeck_CardMask) * 300);
    StdDeck_CardMask *hands2 = malloc(sizeof(StdDeck_CardMask) * 300);
    
    // Test 1: Small (quick test)
    printf("\n--- Test 1: Small Range (AA-KK vs QQ-JJ) ---");
    int count1 = generatePocketPairs(hands1, StdDeck_Rank_KING, StdDeck_Rank_ACE);
    int count2 = generatePocketPairs(hands2, StdDeck_Rank_JACK, StdDeck_Rank_QUEEN);
    PlayerRange test1[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_comprehensive_benchmark("Small Range Test", test1);
    
    // Test 2: Medium
    printf("\n--- Test 2: Medium Range ---");
    count1 = generateMediumRange(hands1);
    count2 = generateMediumRange(hands2);
    PlayerRange test2[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_comprehensive_benchmark("Medium Range Test", test2);
    
    // Test 3: Large
    printf("\n--- Test 3: Large Range (All pairs vs Medium) ---");
    count1 = generatePocketPairs(hands1, StdDeck_Rank_2, StdDeck_Rank_ACE);
    count2 = generateMediumRange(hands2);
    PlayerRange test3[2] = {
        {.hand_masks = hands1, .weights = NULL, .count = count1, .total_weight = count1},
        {.hand_masks = hands2, .weights = NULL, .count = count2, .total_weight = count2}}
        ;
    run_comprehensive_benchmark("Large Range Test", test3);
    
    free(hands1);
    free(hands2);
    
    printf("\n==============================================\n");
    printf("BENCHMARK COMPLETE\n");
    printf("==============================================\n");
    
    return 0;
}
