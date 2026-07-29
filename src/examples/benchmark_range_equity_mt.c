/*
 * benchmark_range_equity_mt.c - Benchmark multithreaded range equity calculations
 * 
 * This program compares the performance of single-threaded vs multithreaded
 * range equity calculations with various range sizes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "poker_eval/utils/omp_compat.h"

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>
#include <poker_eval/deck/deck_std.h> // For StdDeck_RANK_OF_CARD and StdDeck_SUIT_OF_CARD
#include <math.h>    // For fabs

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

// Generate suited connectors
static int generateSuitedConnectors(StdDeck_CardMask *hands, int minRank, int maxRank) {
    int count = 0;
    for (int rank = minRank; rank < maxRank && rank < StdDeck_Rank_ACE; rank++) {
        for (int suit = 0; suit < 4; suit++) {
            StdDeck_CardMask_RESET(hands[count]);
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit));
            StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank + 1, suit));
            count++;
        }
    }
    return count;
}

// Generate broadway cards (T-A)
static int generateBroadwayHands(StdDeck_CardMask *hands) {
    int count = 0;
    int broadway_ranks[] = {StdDeck_Rank_TEN, StdDeck_Rank_JACK, 
                           StdDeck_Rank_QUEEN, StdDeck_Rank_KING, StdDeck_Rank_ACE};
    
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            for (int suit1 = 0; suit1 < 4; suit1++) {
                for (int suit2 = 0; suit2 < 4; suit2++) {
                    if (broadway_ranks[i] == broadway_ranks[j] && suit1 >= suit2) continue;
                    
                    StdDeck_CardMask_RESET(hands[count]);
                    StdDeck_CardMask_SET(hands[count], 
                        StdDeck_MAKE_CARD(broadway_ranks[i], suit1));
                    StdDeck_CardMask_SET(hands[count], 
                        StdDeck_MAKE_CARD(broadway_ranks[j], suit2));
                    count++;
                }
            }
        }
    }
    return count;
}

// Benchmark configuration
typedef struct {
    const char *name;
    const char *range1_desc;
    const char *range2_desc;
    int use_board;
    StdDeck_CardMask board;
} BenchmarkScenario;

static void run_benchmark(BenchmarkScenario *scenario, StdDeck_CardMask *range1_hands, int range1_count,
                   StdDeck_CardMask *range2_hands, int range2_count) {
    
    printf("\n=== Benchmark: %s ===\n", scenario->name);
    printf("Range 1: %s (%d combos)\n", scenario->range1_desc, range1_count);
    printf("Range 2: %s (%d combos)\n", scenario->range2_desc, range2_count);
    
    if (scenario->use_board) {
        printf("Board: ");
        for (int card = 0; card < 52; card++) {
            if (StdDeck_CardMask_CARD_IS_SET(scenario->board, card)) {
                int r = StdDeck_RANK(card);
                int s = StdDeck_SUIT(card);
                printf("%c%c ", StdDeck_rankChars[r], StdDeck_suitChars[s]);
            }
        }
        printf("\n");
    }
    
    int total_matchups = range1_count * range2_count;
    printf("Total potential matchups: %d\n", total_matchups);
    
    PlayerRange ranges[2] = {
        {range1_hands, NULL, range1_count, (double)range1_count},
        {range2_hands, NULL, range2_count, (double)range2_count}
    };
    
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    
    enum_result_t result_st, result_mt;
    
    // Single-threaded benchmark
    printf("\nSingle-threaded calculation...\n");
    enumResultAlloc(&result_st, 2, enum_ordering_mode_hi);
    
    double start_st = get_time();
    int matchups_st = CalculateEquityForRanges(
        game_holdem, ranges, 2, scenario->board, dead,
        scenario->use_board ? 2 : 5,  // nboard_cards_to_deal
        0,  // exhaustive
        0,  // iterations (not used)
        0,  // orderflag
        &result_st
    );
    double time_st = get_time() - start_st;
    
    printf("Time: %.3f seconds\n", time_st);
    printf("Valid matchups: %d\n", matchups_st);
    printf("Matchups/second: %.0f\n", matchups_st / time_st);
    printf("Results: P1=%.3f%% P2=%.3f%%\n", 
           result_st.ev[0] * 100, result_st.ev[1] * 100);
    
    // Multi-threaded benchmarks with different thread counts
    int thread_counts[] = {2, 4, 8, 0};  // 0 = auto-detect
    const char *thread_labels[] = {"2 threads", "4 threads", "8 threads", "auto"};
    
    for (int i = 0; i < 4; i++) {
        int num_threads = thread_counts[i];
        if (num_threads == 0) {
            num_threads = omp_get_max_threads();
        }
        
        printf("\nMulti-threaded calculation (%s)...\n", thread_labels[i]);
        enumResultAlloc(&result_mt, 2, enum_ordering_mode_hi);
        
        double start_mt = get_time();
        int matchups_mt = CalculateEquityForRanges_MT(
            game_holdem, ranges, 2, scenario->board, dead,
            scenario->use_board ? 2 : 5,
            0,  // exhaustive
            0,  // iterations
            0,  // orderflag
            &result_mt,
            thread_counts[i]
        );
        double time_mt = get_time() - start_mt;
        
        printf("Time: %.3f seconds\n", time_mt);
        printf("Valid matchups: %d\n", matchups_mt);
        printf("Matchups/second: %.0f\n", matchups_mt / time_mt);
        printf("Speedup: %.2fx\n", time_st / time_mt);
        printf("Results: P1=%.3f%% P2=%.3f%%\n", 
               result_mt.ev[0] * 100, result_mt.ev[1] * 100);
        
        // Verify results match
        if (fabs(result_st.ev[0] - result_mt.ev[0]) > 0.0001 ||
            fabs(result_st.ev[1] - result_mt.ev[1]) > 0.0001) {
            printf("WARNING: Results don't match!\n");
        }
        
        enumResultFree(&result_mt);

        // --- Benchmark v2 ---
        enum_result_t result_mt_v2;
        enumResultAlloc(&result_mt_v2, 2, enum_ordering_mode_hi);
        double start_mt_v2 = get_time();
        int matchups_mt_v2 = CalculateEquityForRanges_MT_v2(
            game_holdem, ranges, 2, scenario->board, dead,
            scenario->use_board ? 2 : 5,
            0,  // exhaustive
            0,  // iterations
            0,  // orderflag
            &result_mt_v2,
            thread_counts[i]
        );
        double time_mt_v2 = get_time() - start_mt_v2;
        printf("[v2] Time: %.3f seconds\n", time_mt_v2);
        printf("[v2] Valid matchups: %d\n", matchups_mt_v2);
        printf("[v2] Matchups/second: %.0f\n", matchups_mt_v2 / time_mt_v2);
        printf("[v2] Speedup: %.2fx\n", time_st / time_mt_v2);
        printf("[v2] Results: P1=%.3f%% P2=%.3f%%\n", 
               result_mt_v2.ev[0] * 100, result_mt_v2.ev[1] * 100);
        if (fabs(result_st.ev[0] - result_mt_v2.ev[0]) > 0.0001 ||
            fabs(result_st.ev[1] - result_mt_v2.ev[1]) > 0.0001) {
            printf("[v2] WARNING: Results don't match!\n");
        }
        enumResultFree(&result_mt_v2);

        // --- Benchmark v5 ---
        enum_result_t result_mt_v5;
        enumResultAlloc(&result_mt_v5, 2, enum_ordering_mode_hi);
        double start_mt_v5 = get_time();
        int matchups_mt_v5 = CalculateEquityForRanges_MT_v5(
            game_holdem, ranges, 2, scenario->board, dead,
            scenario->use_board ? 2 : 5,
            0,  // exhaustive
            0,  // iterations
            0,  // orderflag
            &result_mt_v5,
            thread_counts[i]
        );
        double time_mt_v5 = get_time() - start_mt_v5;
        printf("[v5] Time: %.3f seconds\n", time_mt_v5);
        printf("[v5] Valid matchups: %d\n", matchups_mt_v5);
        printf("[v5] Matchups/second: %.0f\n", matchups_mt_v5 / time_mt_v5);
        printf("[v5] Speedup: %.2fx\n", time_st / time_mt_v5);
        printf("[v5] Results: P1=%.3f%% P2=%.3f%%\n", 
               result_mt_v5.ev[0] * 100, result_mt_v5.ev[1] * 100);
        if (fabs(result_st.ev[0] - result_mt_v5.ev[0]) > 0.0001 ||
            fabs(result_st.ev[1] - result_mt_v5.ev[1]) > 0.0001) {
            printf("[v5] WARNING: Results don't match!\n");
        }
        enumResultFree(&result_mt_v5);
    }
    
    enumResultFree(&result_st);
}

int main(int argc, char *argv[]) {
    printf("==============================================\n");
    printf("RANGE EQUITY MULTITHREADING BENCHMARK\n");
    printf("==============================================\n");
    printf("Max threads available: %d\n", omp_get_max_threads());
    
    // Allocate space for hands
    StdDeck_CardMask *range1_hands = malloc(sizeof(StdDeck_CardMask) * 1000);
    StdDeck_CardMask *range2_hands = malloc(sizeof(StdDeck_CardMask) * 1000);
    
    // Scenario 1: Small ranges (pairs vs pairs)
    BenchmarkScenario scenario1 = {
        .name = "Small ranges: AA-QQ vs JJ-99",
        .range1_desc = "AA-QQ",
        .range2_desc = "JJ-99",
        .use_board = 0
    };
    
    int range1_count = generatePocketPairs(range1_hands, StdDeck_Rank_QUEEN, StdDeck_Rank_ACE);
    int range2_count = generatePocketPairs(range2_hands, StdDeck_Rank_9, StdDeck_Rank_JACK);
    
    run_benchmark(&scenario1, range1_hands, range1_count, range2_hands, range2_count);
    
    // Scenario 2: Medium ranges
    BenchmarkScenario scenario2 = {
        .name = "Medium ranges: All pairs vs Broadway hands",
        .range1_desc = "22+",
        .range2_desc = "Broadway",
        .use_board = 0
    };
    
    range1_count = generatePocketPairs(range1_hands, StdDeck_Rank_2, StdDeck_Rank_ACE);
    range2_count = generateBroadwayHands(range2_hands);
    
    run_benchmark(&scenario2, range1_hands, range1_count, range2_hands, range2_count);
    
    // Scenario 3: Large ranges with board
    BenchmarkScenario scenario3 = {
        .name = "Large ranges with flop: Pairs vs Suited Connectors on 9s8s2h",
        .range1_desc = "66+",
        .range2_desc = "Suited connectors",
        .use_board = 1
    };
    
    // Set up board: 9s 8s 2h
    StdDeck_CardMask_RESET(scenario3.board);
    StdDeck_CardMask_SET(scenario3.board, StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(scenario3.board, StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(scenario3.board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_HEARTS));
    
    range1_count = generatePocketPairs(range1_hands, StdDeck_Rank_6, StdDeck_Rank_ACE);
    range2_count = generateSuitedConnectors(range2_hands, StdDeck_Rank_2, StdDeck_Rank_ACE);
    
    run_benchmark(&scenario3, range1_hands, range1_count, range2_hands, range2_count);
    
    // Temporarily stop after scenario 3 for quicker testing
    // free(range1_hands);
    // free(range2_hands);
    // printf("\n");
    // BenchmarkScenario scenario4 = {
    //     .name = "Stress test: All pairs vs All broadway + suited connectors",
    //     .range1_desc = "22+",
    //     .range2_desc = "Broadway + SC",
    //     .use_board = 0
    // };
    // 
    // range1_count = generatePocketPairs(range1_hands, StdDeck_Rank_2, StdDeck_Rank_ACE);
    // range2_count = generateBroadwayHands(range2_hands);
    // range2_count += generateSuitedConnectors(&range2_hands[range2_count], 
    //                                        StdDeck_Rank_2, StdDeck_Rank_ACE);
    // 
    // run_benchmark(&scenario4, range1_hands, range1_count, range2_hands, range2_count);
    
    // Cleanup
    free(range1_hands);
    free(range2_hands);
    
    printf("\n==============================================\n");
    printf("BENCHMARK COMPLETE\n");
    printf("==============================================\n");
    
    return 0;
}
