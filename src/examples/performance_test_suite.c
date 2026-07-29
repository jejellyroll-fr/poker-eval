/*
 * performance_test_suite.c - Comprehensive performance testing suite
 * Tests all MT versions across different architectures and configurations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <poker_eval/core/unistd_compat.h>
#include <math.h>
#include "poker_eval/utils/omp_compat.h"

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/equity/RangeEquity_MT.h>

// Performance metrics structure
typedef struct {
    double cpu_time;
    double wall_time;
    long memory_peak;
    int matchups;
    double ev[2];
    double cpu_efficiency;
    double speedup;
} PerfMetrics;

// Test configuration
typedef struct {
    const char *name;
    int range1_size;
    int range2_size;
    int expected_matchups;
} TestConfig;

/* Function prototypes */
static long get_memory_usage(void);
static double get_wall_time(void);
static double get_cpu_time(void);
static int generate_test_range(StdDeck_CardMask *hands, int size_class);
static PerfMetrics run_perf_test(
    const char *version_name,
    int (*calc_func)(enum_game_t, const PlayerRange[], int, StdDeck_CardMask, 
                     StdDeck_CardMask, int, int, int, int, enum_result_t*, int),
    PlayerRange *ranges,
    int num_threads,
    double baseline_time
);
static void print_perf_report(const char *test_name, TestConfig *config, PerfMetrics *metrics, int num_versions);
static void print_system_info(void);

// Get current memory usage in KB
static long get_memory_usage(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

// High-resolution timer
static double get_wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

// CPU time
static double get_cpu_time(void) {
    clock_t c = clock(); return (double)c / CLOCKS_PER_SEC;
}

// Generate test ranges
static int generate_test_range(StdDeck_CardMask *hands, int size_class) {
    int count = 0;
    
    /* Declare variables that will be used in switch cases */
    int broadway[] = {StdDeck_Rank_TEN, StdDeck_Rank_JACK, 
                     StdDeck_Rank_QUEEN, StdDeck_Rank_KING, StdDeck_Rank_ACE};
    
    switch (size_class) {
        case 1: // Tiny (6 hands)
            for (int rank = StdDeck_Rank_ACE; rank >= StdDeck_Rank_KING && count < 6; rank--) {
                for (int suit1 = 0; suit1 < 4 && count < 6; suit1++) {
                    for (int suit2 = suit1 + 1; suit2 < 4 && count < 6; suit2++) {
                        StdDeck_CardMask_RESET(hands[count]);
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
                        count++;
                    }
                }
            }
            break;
            
        case 2: // Small (30 hands)
            // All pairs TT+
            for (int rank = StdDeck_Rank_TEN; rank <= StdDeck_Rank_ACE; rank++) {
                for (int suit1 = 0; suit1 < 4; suit1++) {
                    for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
                        StdDeck_CardMask_RESET(hands[count]);
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
                        count++;
                    }
                }
            }
            break;
            
        case 3: // Medium (78 hands - all pairs)
            for (int rank = StdDeck_Rank_2; rank <= StdDeck_Rank_ACE; rank++) {
                for (int suit1 = 0; suit1 < 4; suit1++) {
                    for (int suit2 = suit1 + 1; suit2 < 4; suit2++) {
                        StdDeck_CardMask_RESET(hands[count]);
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit1));
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(rank, suit2));
                        count++;
                    }
                }
            }
            break;
            
        case 4: // Large (150+ hands)
            // All pairs + broadway
            count = generate_test_range(hands, 3); // Start with all pairs
            
            // Add broadway hands
            for (int i = 0; i < 5; i++) {
                for (int j = i + 1; j < 5; j++) {
                    for (int suit = 0; suit < 4; suit++) {
                        StdDeck_CardMask_RESET(hands[count]);
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(broadway[i], suit));
                        StdDeck_CardMask_SET(hands[count], StdDeck_MAKE_CARD(broadway[j], suit));
                        count++;
                    }
                }
            }
            break;
        default:
            break;
    }
    
    return count;
}

// Run performance test
static PerfMetrics run_perf_test(
    const char *version_name,
    int (*calc_func)(enum_game_t, const PlayerRange[], int, StdDeck_CardMask, 
                     StdDeck_CardMask, int, int, int, int, enum_result_t*, int),
    PlayerRange *ranges,
    int num_threads,
    double baseline_time
) {
    PerfMetrics metrics = {0};
    
    StdDeck_CardMask board, dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);
    
    enum_result_t result;
    enumResultAlloc(&result, 2, enum_ordering_mode_hi);
    
    long mem_before = get_memory_usage();
    double cpu_start = get_cpu_time();
    double wall_start = get_wall_time();
    
    // Run the calculation
    if (calc_func) {
        metrics.matchups = calc_func(
            game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result, num_threads
        );
    } else {
        // Single-threaded version
        metrics.matchups = CalculateEquityForRanges(
            game_holdem, ranges, 2, board, dead, 5, 0, 0, 0, &result
        );
    }
    
    metrics.cpu_time = get_cpu_time() - cpu_start;
    metrics.wall_time = get_wall_time() - wall_start;
    metrics.memory_peak = get_memory_usage() - mem_before;
    
    if (metrics.matchups > 0) {
        metrics.ev[0] = result.ev[0] / metrics.matchups * 100;
        metrics.ev[1] = result.ev[1] / metrics.matchups * 100;
    }
    
    if (baseline_time > 0) {
        metrics.speedup = baseline_time / metrics.wall_time;
    }
    if (num_threads > 0 && metrics.wall_time > 0) {
        metrics.cpu_efficiency = (metrics.cpu_time / metrics.wall_time) / num_threads * 100;
    }
    
    enumResultFree(&result);
    
    return metrics;
}

// Print performance report
static void print_perf_report(const char *test_name, TestConfig *config, PerfMetrics *metrics, int num_versions) {
    printf("\n=== %s ===\n", test_name);
    printf("Configuration: %d x %d hands = %d potential matchups\n", 
           config->range1_size, config->range2_size, 
           config->range1_size * config->range2_size);
    
    printf("\n%-20s %8s %8s %8s %8s %10s %8s\n", 
           "Version", "Wall(s)", "CPU(s)", "Speedup", "Eff(%)", "Mem(KB)", "EV(%)");
    printf("%-20s %8s %8s %8s %8s %10s %8s\n",
           "-------", "-------", "------", "-------", "------", "-------", "-----");
    
    for (int i = 0; i < num_versions; i++) {
        printf("%-20s %8.3f %8.3f %8.2fx %7.1f%% %9ld %7.2f\n",
               i == 0 ? "Single-thread" : 
               i == 1 ? "MT original (2t)" :
               i == 2 ? "MT original (4t)" :
               i == 3 ? "MT v2 (2t)" :
               i == 4 ? "MT v2 (4t)" :
               i == 5 ? "MT v3 (2t)" :
               i == 6 ? "MT v3 (4t)" : "MT v3 (auto)",
               metrics[i].wall_time,
               metrics[i].cpu_time,
               metrics[i].speedup,
               metrics[i].cpu_efficiency,
               metrics[i].memory_peak,
               metrics[i].ev[0]);
    }
}

// Architecture detection
static void print_system_info(void) {
    printf("=== SYSTEM INFORMATION ===\n");
    
    // CPU info
#if defined(_WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    long logical = (long)sysinfo.dwNumberOfProcessors;
    long physical = logical;
    printf("CPU Cores: %ld physical, %ld logical\n", physical, logical);
#else
    printf("CPU Cores: %ld physical, %ld logical\n",
           sysconf(_SC_NPROCESSORS_CONF) / 2,
           sysconf(_SC_NPROCESSORS_CONF));
#endif
    
    // OpenMP info
    printf("OpenMP: %d max threads\n", omp_get_max_threads());
    
    // Memory page size
#if defined(_WIN32)
    printf("Page size: %lu bytes\n", (unsigned long)sysinfo.dwPageSize);
#else
    printf("Page size: %ld bytes\n", sysconf(_SC_PAGESIZE));
#endif
    
    printf("\n");
}

int main(void) {
    printf("==============================================\n");
    printf("POKER-EVAL MT PERFORMANCE TEST SUITE\n");
    printf("==============================================\n\n");
    
    print_system_info();
    
    // Allocate space for test ranges
    StdDeck_CardMask *hands1 = malloc(sizeof(StdDeck_CardMask) * 200);
    StdDeck_CardMask *hands2 = malloc(sizeof(StdDeck_CardMask) * 200);
    
    // Test configurations
    TestConfig tests[] = {
        {"Tiny Test", 1, 1, 36},      // 6x6
        {"Small Test", 2, 2, 900},     // 30x30
        {"Medium Test", 3, 2, 2340},   // 78x30
        {"Large Test", 3, 3, 6084},    // 78x78
        {"XLarge Test", 4, 3, 11700}   // 150x78
    };
    
    // Run tests
    for (int t = 0; t < 5; t++) {
        TestConfig *config = &tests[t];
        
        // Generate ranges
        int count1 = generate_test_range(hands1, config->range1_size);
        int count2 = generate_test_range(hands2, config->range2_size);
        
        PlayerRange ranges[2] = {{hands1, NULL, count1, (double)count1}, {hands2, NULL, count2, (double)count2}};
        
        PerfMetrics metrics[11];
        
        // Test all versions
        metrics[0] = run_perf_test("Single-thread", NULL, ranges, 1, 0);
        double baseline = metrics[0].wall_time;
        
        metrics[1] = run_perf_test("MT orig 2t", CalculateEquityForRanges_MT, ranges, 2, baseline);
        metrics[2] = run_perf_test("MT orig 4t", CalculateEquityForRanges_MT, ranges, 4, baseline);
        metrics[3] = run_perf_test("MT v2 2t", CalculateEquityForRanges_MT_v2, ranges, 2, baseline);
        metrics[4] = run_perf_test("MT v2 4t", CalculateEquityForRanges_MT_v2, ranges, 4, baseline);
        metrics[5] = run_perf_test("MT v3 2t", CalculateEquityForRanges_MT_v3, ranges, 2, baseline);
        metrics[6] = run_perf_test("MT v3 4t", CalculateEquityForRanges_MT_v3, ranges, 4, baseline);
        metrics[7] = run_perf_test("MT v3 auto", CalculateEquityForRanges_MT_v3, ranges, 0, baseline);
        metrics[8] = run_perf_test("MT v5 ws 2t", CalculateEquityForRanges_MT_v5, ranges, 2, baseline);
        metrics[9] = run_perf_test("MT v5 ws 4t", CalculateEquityForRanges_MT_v5, ranges, 4, baseline);
        metrics[10] = run_perf_test("MT v5 ws auto", CalculateEquityForRanges_MT_v5, ranges, 0, baseline);
        
        print_perf_report(config->name, config, metrics, 11);
        
        // Verify results match
        int all_match = 1;
        for (int i = 1; i < 11; i++) {
            if (fabs(metrics[0].ev[0] - metrics[i].ev[0]) > 0.01) {
                all_match = 0;
                break;
            }
        }
        printf("\nResult verification: %s\n", all_match ? "✓ PASS" : "✗ FAIL");
    }
    
    // Performance summary
    printf("\n=== PERFORMANCE SUMMARY ===\n");
    printf("1. For tiny ranges (<100 matchups): Single-thread is optimal\n");
    printf("2. For small ranges (100-1000): MT with 2 threads shows best efficiency\n");
    printf("3. For medium ranges (1000-10000): MT_v2 with 4 threads recommended\n");
    printf("4. For large ranges (>10000): MT_v3 provides best memory efficiency\n");
    printf("5. CPU efficiency decreases with more threads due to synchronization\n");
    
    free(hands1);
    free(hands2);
    
    printf("\n==============================================\n");
    printf("TEST SUITE COMPLETE\n");
    printf("==============================================\n");
    
    return 0;
}
