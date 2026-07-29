/*
 * Comprehensive benchmarking suite for Advanced Range Parser
 * Measures performance across different range types and sizes
 */

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <poker_eval/range/AdvancedRangeParser.h>

#define ITERATIONS 1000
#define WARMUP_ITERATIONS 100

/* Benchmark result structure */
typedef struct {
    const char *name;
    const char *range_string;
    enum_game_t game_type;
    double avg_time_us;
    size_t hands_count;
    double hands_per_second;
} bench_result_t;

/* Timing utilities */
static double get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000.0 + ts.tv_nsec / 1000.0;
}

/* Run single benchmark */
static bench_result_t run_benchmark(
    const char *name,
    const char *range_string,
    enum_game_t game_type,
    int iterations)
{
    bench_result_t result = {0};
    result.name = name;
    result.range_string = range_string;
    result.game_type = game_type;

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Warmup */
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        arp_range_t range;
        ARP_ParseRange(range_string, dead, game_type, &range);
        result.hands_count = range.count;
        ARP_FreeRange(&range);
    }

    /* Actual benchmark */
    double total_time = 0.0;
    for (int i = 0; i < iterations; i++) {
        arp_range_t range;

        double start = get_time_us();
        int success = ARP_ParseRange(range_string, dead, game_type, &range);
        double end = get_time_us();

        if (success) {
            total_time += (end - start);
            ARP_FreeRange(&range);
        }
    }

    result.avg_time_us = total_time / iterations;
    result.hands_per_second = (double)result.hands_count / (result.avg_time_us / 1000000.0);

    return result;
}

/* Print results table */
static void print_results_header(void) {
    printf("\n%-40s %12s %10s %15s\n",
           "Test Case", "Time (µs)", "Hands", "Hands/sec");
    printf("%-40s %12s %10s %15s\n",
           "----------------------------------------",
           "------------", "----------", "---------------");
}

static void print_result(const bench_result_t *r) {
    printf("%-40s %12.2f %10zu %15.0f\n",
           r->name, r->avg_time_us, r->hands_count, r->hands_per_second);
}

/* Benchmark categories */

static void benchmark_basic_hands(void) {
    printf("\n=== Basic Hands ===\n");
    print_results_header();

    bench_result_t results[] = {
        run_benchmark("Single pair (AA)", "AA", game_holdem, ITERATIONS),
        run_benchmark("Suited hand (AKs)", "AKs", game_holdem, ITERATIONS),
        run_benchmark("Offsuit hand (AKo)", "AKo", game_holdem, ITERATIONS),
        run_benchmark("Both (AK)", "AK", game_holdem, ITERATIONS),
        run_benchmark("Specific hand (AsKh)", "AsKh", game_holdem, ITERATIONS)
    };

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        print_result(&results[i]);
    }
}

static void benchmark_ranges(void) {
    printf("\n=== Range Expressions ===\n");
    print_results_header();

    bench_result_t results[] = {
        run_benchmark("Pair range (AA-TT)", "AA-TT", game_holdem, ITERATIONS),
        run_benchmark("Non-pair range (AK-AJ)", "AK-AJ", game_holdem, ITERATIONS),
        run_benchmark("Non-pair suited (AKs-AJs)", "AKs-AJs", game_holdem, ITERATIONS),
        run_benchmark("Large pair range (AA-22)", "AA-22", game_holdem, ITERATIONS),
        run_benchmark("Large non-pair (AK-A2)", "AK-A2", game_holdem, ITERATIONS)
    };

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        print_result(&results[i]);
    }
}

static void benchmark_percentages(void) {
    printf("\n=== Percentage Ranges ===\n");
    print_results_header();

    bench_result_t results[] = {
        run_benchmark("Top 1%", "1%", game_holdem, ITERATIONS),
        run_benchmark("Top 5%", "5%", game_holdem, ITERATIONS),
        run_benchmark("Top 10%", "10%", game_holdem, ITERATIONS),
        run_benchmark("Top 20%", "20%", game_holdem, ITERATIONS),
        run_benchmark("Top 50%", "50%", game_holdem, ITERATIONS)
    };

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        print_result(&results[i]);
    }
}

static void benchmark_operations(void) {
    printf("\n=== Operations ===\n");
    print_results_header();

    bench_result_t results[] = {
        run_benchmark("Addition (AA + KK)", "AA + KK", game_holdem, ITERATIONS),
        run_benchmark("Subtraction (AA-TT - QQ)", "AA-TT - QQ", game_holdem, ITERATIONS),
        run_benchmark("Complex (AA-TT + AK-AJ)", "AA-TT + AK-AJ", game_holdem, ITERATIONS),
        run_benchmark("Percentage op (20% - AA)", "20% - AA", game_holdem, ITERATIONS),
        run_benchmark("Exclusion (!AA)", "!AA", game_holdem, ITERATIONS)
    };

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        print_result(&results[i]);
    }
}

static void benchmark_omaha(void) {
    printf("\n=== Omaha/PLO ===\n");
    print_results_header();

    bench_result_t results[] = {
        run_benchmark("PLO pattern (AAxxds)", "AAxxds", game_omaha, ITERATIONS / 10),
        run_benchmark("PLO rundown (JT98r)", "JT98r", game_omaha, ITERATIONS / 10),
        run_benchmark("PLO category (cat:aa_ds)", "cat:aa_ds", game_omaha, ITERATIONS / 10),
        run_benchmark("PLO percentage (20%)", "20%", game_omaha, ITERATIONS / 10)
    };

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        print_result(&results[i]);
    }
}

/* Cache performance benchmark */
static void benchmark_cache_performance(void) {
    printf("\n=== Cache Performance ===\n");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Clear cache */
    ARP_ClearCache();

    /* First parse (cold cache) */
    double start = get_time_us();
    for (int i = 0; i < 100; i++) {
        arp_range_t range;
        ARP_ParseRange("20%", dead, game_holdem, &range);
        ARP_FreeRange(&range);
        ARP_ClearCache(); /* Force cache miss */
    }
    double cold_time = (get_time_us() - start) / 100.0;

    /* Second parse (warm cache) */
    ARP_ClearCache();
    start = get_time_us();
    for (int i = 0; i < 100; i++) {
        arp_range_t range;
        ARP_ParseRange("20%", dead, game_holdem, &range);
        ARP_FreeRange(&range);
        /* Don't clear - use cache */
    }
    double warm_time = (get_time_us() - start) / 100.0;

    printf("\n%-40s %12.2f µs\n", "Cold cache (no reuse)", cold_time);
    printf("%-40s %12.2f µs\n", "Warm cache (reuse)", warm_time);
    printf("%-40s %12.1fx\n", "Speedup", cold_time / warm_time);

    /* Cache statistics */
    arp_cache_stats_t stats;
    ARP_GetCacheStats(&stats);
    printf("\nCache Statistics:\n");
    printf("  Valid entries: %d / %d\n", stats.valid_entries, stats.total_entries);
    printf("  Memory used: %zu bytes\n", stats.total_memory_bytes);
}

/* Memory efficiency benchmark */
static void benchmark_memory_efficiency(void) {
    printf("\n=== Memory Efficiency ===\n");
    printf("\n%-40s %10s %10s %10s\n",
           "Range", "Hands", "Capacity", "Efficiency");
    printf("%-40s %10s %10s %10s\n",
           "----------------------------------------",
           "----------", "----------", "----------");

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    struct {
        const char *name;
        const char *range_string;
    } test_cases[] = {
        {"Small (AA)", "AA"},
        {"Medium (AA-TT)", "AA-TT"},
        {"Large (AA-22)", "AA-22"},
        {"Very large (AA-22, AK-A2)", "AA-22, AK-A2"},
        {"Percentage (20%)", "20%"}
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        arp_range_t range;
        if (ARP_ParseRange(test_cases[i].range_string, dead, game_holdem, &range)) {
            double efficiency = 100.0 * (double)range.count / (double)range.capacity;
            printf("%-40s %10zu %10zu %9.1f%%\n",
                   test_cases[i].name, range.count, range.capacity, efficiency);
            ARP_FreeRange(&range);
        }
    }
}

/* Main benchmark runner */
int main(int argc, char **argv) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   Advanced Range Parser - Performance Benchmark Suite     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    /* Initialize cache */
    ARP_InitCache();

    /* Run benchmark categories */
    if (argc > 1 && strcmp(argv[1], "quick") == 0) {
        printf("\n🚀 Quick Benchmark Mode (fewer iterations)\n");
        benchmark_basic_hands();
        benchmark_percentages();
    } else {
        printf("\n🔬 Full Benchmark Mode\n");
        printf("Iterations per test: %d (+ %d warmup)\n", ITERATIONS, WARMUP_ITERATIONS);

        benchmark_basic_hands();
        benchmark_ranges();
        benchmark_percentages();
        benchmark_operations();
        benchmark_omaha();
        benchmark_cache_performance();
        benchmark_memory_efficiency();
    }

    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║   Benchmark Complete                                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");

    return 0;
}
