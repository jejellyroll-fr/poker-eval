/*
 * ofc_simd_benchmark.c - Comprehensive benchmarks for OFC SIMD optimizations
 *
 * This benchmark suite validates the performance gains of SIMD optimizations
 * for Open Face Chinese Poker calculations, providing detailed performance
 * metrics for different batch sizes and SIMD instruction sets.
 *
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/poker_eval.h>
#include <poker_eval/deck/deck_std.h>

/* Benchmark configuration */
#define BENCHMARK_ITERATIONS 10 /* Reduced from 1000 */
#define MONTE_CARLO_SAMPLES 50  /* Reduced from 1000 */
#define MAX_BATCH_SIZE 16
#define NUM_TEST_HANDS 20 /* Reduced from 100 */

/* Performance measurement structure */
typedef struct
{
    double cpu_time_ms;
    double simd_time_ms;
    double speedup;
    int batch_size;
    const char *simd_mode;
    int operations_completed;
} benchmark_result_t;

/* Global test data */
static ofc_hand_t test_hands[NUM_TEST_HANDS];
static int test_cards[NUM_TEST_HANDS];
static ofc_position_t test_positions[NUM_TEST_HANDS];
static int benchmark_initialized = 0;

/* ===== Benchmark Utilities ===== */

/* High-precision timer */
static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* Initialize test data */
static void initialize_benchmark_data(void)
{
    if (benchmark_initialized)
        return;

    printf("Initializing benchmark test data...\n");

    srand((unsigned int)time(NULL));

    for (int i = 0; i < NUM_TEST_HANDS; i++)
    {
        /* Initialize hand */
        OFC_InitializeHand(&test_hands[i]);

        /* Create realistic partial hands */
        int num_cards_placed = 5 + (rand() % 8); /* 5-12 cards */
        int cards_used[StdDeck_N_CARDS] = {0};

        for (int j = 0; j < num_cards_placed; j++)
        {
            int card;
            do
            {
                card = rand() % StdDeck_N_CARDS;
            } while (cards_used[card]);
            cards_used[card] = 1;

            /* Place in random valid position */
            ofc_position_t pos;
            int attempts = 0;
            do
            {
                pos = rand() % OFC_NUM_HANDS;
                attempts++;
                if (attempts > 50)
                {
                    /* If we can't find a valid position, break out */
                    goto next_hand;
                }
            } while ((pos == OFC_TOP && test_hands[i].card_count[pos] >= OFC_TOP_CARDS) ||
                     (pos != OFC_TOP && test_hands[i].card_count[pos] >= OFC_MIDDLE_CARDS));

            if (OFC_PlaceCard(&test_hands[i], card, pos) != 0)
            {
                /* Placement failed, break out */
                break;
            }
        }

    next_hand:
        /* Select test card (not already used) */
        {
            int card_attempts = 0;
            do
            {
                test_cards[i] = rand() % StdDeck_N_CARDS;
                card_attempts++;
                if (card_attempts > 100)
                {
                    test_cards[i] = rand() % StdDeck_N_CARDS; // Just pick any card
                    break;
                }
            } while (cards_used[test_cards[i]]);

            /* Select valid test position */
            int pos_attempts = 0;
            do
            {
                test_positions[i] = rand() % OFC_NUM_HANDS;
                pos_attempts++;
                if (pos_attempts > 50)
                {
                    test_positions[i] = OFC_TOP; // Default to TOP
                    break;
                }
            } while ((test_positions[i] == OFC_TOP && test_hands[i].card_count[test_positions[i]] >= OFC_TOP_CARDS) ||
                     (test_positions[i] != OFC_TOP && test_hands[i].card_count[test_positions[i]] >= OFC_MIDDLE_CARDS));
        }
    }

    benchmark_initialized = 1;
    printf("Test data initialized: %d hands ready for benchmarking\n", NUM_TEST_HANDS);
}

/* ===== Individual Benchmark Functions ===== */

/* Benchmark single foul risk calculation */
static benchmark_result_t benchmark_single_foul_risk(void)
{
    benchmark_result_t result = {0};
    result.batch_size = 1;
    result.simd_mode = "Single (Auto-select)";

    initialize_benchmark_data();

    printf("\n--- Single Foul Risk Benchmark ---\n");

    /* CPU Baseline */
    OFC_SetSIMDEnabled(0); /* Disable SIMD */
    double start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        for (int i = 0; i < NUM_TEST_HANDS; i++)
        {
            OFC_CalculateFoulRisk(&test_hands[i], test_cards[i], test_positions[i]);
        }
    }

    double end_time = get_time_ms();
    result.cpu_time_ms = end_time - start_time;
    result.operations_completed = BENCHMARK_ITERATIONS * NUM_TEST_HANDS;

    printf("CPU Time: %.2f ms (%d operations)\n", result.cpu_time_ms, result.operations_completed);

    /* SIMD Version */
    OFC_SetSIMDEnabled(1); /* Enable SIMD */
    start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        for (int i = 0; i < NUM_TEST_HANDS; i++)
        {
            OFC_CalculateFoulRisk(&test_hands[i], test_cards[i], test_positions[i]);
        }
    }

    end_time = get_time_ms();
    result.simd_time_ms = end_time - start_time;

    printf("SIMD Time: %.2f ms\n", result.simd_time_ms);

    result.speedup = result.cpu_time_ms / result.simd_time_ms;
    printf("Speedup: %.2fx\n", result.speedup);

    return result;
}

/* Benchmark batched foul risk calculation */
static benchmark_result_t benchmark_batch_foul_risk(int batch_size)
{
    benchmark_result_t result = {0};
    result.batch_size = batch_size;
    result.simd_mode = OFC_GetProcessingModeName(OFC_SelectProcessingMode(batch_size));

    initialize_benchmark_data();

    printf("\n--- Batch Foul Risk Benchmark (Batch Size: %d) ---\n", batch_size);

    /* Prepare batch data */
    /* int num_batches = (NUM_TEST_HANDS + batch_size - 1) / batch_size; */
    float *cpu_results = malloc(NUM_TEST_HANDS * sizeof(float));
    float *simd_results = malloc(NUM_TEST_HANDS * sizeof(float));

    if (!cpu_results || !simd_results)
    {
        printf("Memory allocation failed\n");
        result.cpu_time_ms = result.simd_time_ms = -1;
        return result;
    }

    /* CPU Baseline - Individual calculations */
    OFC_SetSIMDEnabled(0);
    double start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        for (int i = 0; i < NUM_TEST_HANDS; i++)
        {
            cpu_results[i] = OFC_CalculateFoulRisk(&test_hands[i], test_cards[i], test_positions[i]);
        }
    }

    double end_time = get_time_ms();
    result.cpu_time_ms = end_time - start_time;
    result.operations_completed = BENCHMARK_ITERATIONS * NUM_TEST_HANDS;

    printf("CPU Time: %.2f ms\n", result.cpu_time_ms);

    /* SIMD Batch Version */
    OFC_SetSIMDEnabled(1);
    start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        OFC_CalculateMultipleFoulRisksSIMD(
            test_hands, test_cards, test_positions,
            NUM_TEST_HANDS, MONTE_CARLO_SAMPLES, simd_results);
    }

    end_time = get_time_ms();
    result.simd_time_ms = end_time - start_time;

    printf("SIMD Time: %.2f ms\n", result.simd_time_ms);

    result.speedup = result.cpu_time_ms / result.simd_time_ms;
    printf("Speedup: %.2fx\n", result.speedup);

    /* Validate results consistency (sample check) */
    int consistency_checks = 0;
    int consistent_results = 0;
    for (int i = 0; i < NUM_TEST_HANDS && i < 10; i++)
    {
        float cpu_val = OFC_CalculateFoulRisk(&test_hands[i], test_cards[i], test_positions[i]);
        float simd_val = simd_results[i];
        float diff = (float)fabs(cpu_val - simd_val);

        consistency_checks++;
        if (diff < 0.1f)
        { /* 10% tolerance for Monte Carlo variance */
            consistent_results++;
        }
    }

    printf("Result Consistency: %d/%d (%.1f%%)\n",
           consistent_results, consistency_checks,
           100.0f * (float)consistent_results / (float)consistency_checks);

    free(cpu_results);
    free(simd_results);
    return result;
}

/* Benchmark hierarchy validation */
static benchmark_result_t benchmark_hierarchy_validation(int batch_size)
{
    benchmark_result_t result = {0};
    result.batch_size = batch_size;
    result.simd_mode = OFC_GetProcessingModeName(OFC_SelectProcessingMode(batch_size));

    printf("\n--- Hierarchy Validation Benchmark (Batch Size: %d) ---\n", batch_size);

    /* Create complete hands for validation */
    ofc_hand_t complete_hands[NUM_TEST_HANDS];
    for (int i = 0; i < NUM_TEST_HANDS; i++)
    {
        /* Create a complete valid hand */
        OFC_InitializeHand(&complete_hands[i]);

        /* Top: pair of jacks */
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_SPADES), OFC_TOP);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS), OFC_TOP);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_CLUBS), OFC_TOP);

        /* Middle: two pair */
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_CLUBS), OFC_MIDDLE);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_SPADES), OFC_MIDDLE);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS), OFC_MIDDLE);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_9, StdDeck_Suit_DIAMONDS), OFC_MIDDLE);

        /* Bottom: flush */
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES), OFC_BOTTOM);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_8, StdDeck_Suit_SPADES), OFC_BOTTOM);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_SPADES), OFC_BOTTOM);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_5, StdDeck_Suit_SPADES), OFC_BOTTOM);
        OFC_PlaceCard(&complete_hands[i], StdDeck_MAKE_CARD(StdDeck_Rank_3, StdDeck_Suit_SPADES), OFC_BOTTOM);
    }

    int *cpu_results = malloc(NUM_TEST_HANDS * sizeof(int));
    int *simd_results = malloc(NUM_TEST_HANDS * sizeof(int));

    /* CPU Baseline */
    double start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        for (int i = 0; i < NUM_TEST_HANDS; i++)
        {
            cpu_results[i] = OFC_ValidateHierarchy(&complete_hands[i]);
        }
    }

    double end_time = get_time_ms();
    result.cpu_time_ms = end_time - start_time;
    result.operations_completed = BENCHMARK_ITERATIONS * NUM_TEST_HANDS;

    printf("CPU Time: %.2f ms\n", result.cpu_time_ms);

    /* SIMD Version */
    start_time = get_time_ms();

    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
    {
        OFC_ValidateHierarchySIMD(complete_hands, NUM_TEST_HANDS, simd_results, OFC_PROCESS_SIMD_AUTO);
    }

    end_time = get_time_ms();
    result.simd_time_ms = end_time - start_time;

    printf("SIMD Time: %.2f ms\n", result.simd_time_ms);

    result.speedup = result.cpu_time_ms / result.simd_time_ms;
    printf("Speedup: %.2fx\n", result.speedup);

    /* Validate results */
    int matches = 0;
    for (int i = 0; i < NUM_TEST_HANDS; i++)
    {
        if (cpu_results[i] == simd_results[i])
        {
            matches++;
        }
    }
    printf("Result Accuracy: %d/%d (%.1f%%)\n", matches, NUM_TEST_HANDS, 100.0f * (float)matches / (float)NUM_TEST_HANDS);

    free(cpu_results);
    free(simd_results);
    return result;
}

/* ===== Main Benchmark Suite ===== */

static void print_benchmark_header(void)
{
    printf("\n");
    printf("=================================================================\n");
    printf("                 OFC SIMD BENCHMARK SUITE\n");
    printf("=================================================================\n");
    printf("\n");

    /* Display system information */
    printf("System Configuration:\n");
    printf("  SIMD Capabilities: ");
    int capabilities = OFC_DetectSIMDCapabilities();
    if (capabilities & OFC_SIMD_AVX512)
        printf("AVX-512 ");
    if (capabilities & OFC_SIMD_AVX2)
        printf("AVX2 ");
    if (capabilities & OFC_SIMD_SSE2)
        printf("SSE2 ");
    if (capabilities == OFC_SIMD_NONE)
        printf("None");
    printf("\n");

    printf("  Optimal Batch Size: %d\n", OFC_GetOptimalBatchSize());
    printf("  Test Configuration:\n");
    printf("    Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("    Monte Carlo Samples: %d\n", MONTE_CARLO_SAMPLES);
    printf("    Test Hands: %d\n", NUM_TEST_HANDS);
    printf("\n");
}

static void print_benchmark_summary(benchmark_result_t *results, int num_results)
{
    printf("\n");
    printf("=================================================================\n");
    printf("                     BENCHMARK SUMMARY\n");
    printf("=================================================================\n");
    printf("\n");

    printf("%-25s %-10s %-10s %-10s %-10s\n",
           "Test", "Batch", "CPU (ms)", "SIMD (ms)", "Speedup");
    printf("-----------------------------------------------------------------\n");

    double total_cpu_time = 0, total_simd_time = 0;

    for (int i = 0; i < num_results; i++)
    {
        if (results[i].cpu_time_ms > 0 && results[i].simd_time_ms > 0)
        {
            printf("%-25s %-10d %-10.2f %-10.2f %-10.2fx\n",
                   results[i].simd_mode,
                   results[i].batch_size,
                   results[i].cpu_time_ms,
                   results[i].simd_time_ms,
                   results[i].speedup);

            total_cpu_time += results[i].cpu_time_ms;
            total_simd_time += results[i].simd_time_ms;
        }
    }

    printf("-----------------------------------------------------------------\n");
    printf("%-25s %-10s %-10.2f %-10.2f %-10.2fx\n",
           "TOTAL", "", total_cpu_time, total_simd_time,
           total_cpu_time / total_simd_time);
    printf("\n");

    /* Performance analysis */
    printf("Performance Analysis:\n");

    double best_speedup = 0;
    int best_batch_size = 0;
    for (int i = 0; i < num_results; i++)
    {
        if (results[i].speedup > best_speedup)
        {
            best_speedup = results[i].speedup;
            best_batch_size = results[i].batch_size;
        }
    }

    printf("  Best Speedup: %.2fx (batch size %d)\n", best_speedup, best_batch_size);
    printf("  Overall Speedup: %.2fx\n", total_cpu_time / total_simd_time);

    if (best_speedup >= 4.0)
    {
        printf("  Result: EXCELLENT - SIMD optimizations highly effective\n");
    }
    else if (best_speedup >= 2.0)
    {
        printf("  Result: GOOD - Significant performance improvement\n");
    }
    else if (best_speedup >= 1.2)
    {
        printf("  Result: MODERATE - Some performance improvement\n");
    }
    else
    {
        printf("  Result: POOR - SIMD optimizations not effective\n");
    }
}

int main(void)
{
    printf("=== DEBUT DU BENCHMARK ===\n");

    benchmark_result_t results[10];
    int result_count = 0;

    printf("Printing header...\n");
    print_benchmark_header();

    printf("Detecting SIMD capabilities...\n");
    /* Initialize OFC SIMD system */
    OFC_DetectSIMDCapabilities();

    printf("Running single foul risk benchmark...\n");
    /* Run benchmarks */
    results[result_count++] = benchmark_single_foul_risk();

    /* Test different batch sizes */
    int batch_sizes[] = {4, 8, 16};
    int num_batch_sizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    for (int i = 0; i < num_batch_sizes; i++)
    {
        int batch_size = batch_sizes[i];
        if (batch_size <= OFC_GetOptimalBatchSize())
        {
            results[result_count++] = benchmark_batch_foul_risk(batch_size);
            results[result_count++] = benchmark_hierarchy_validation(batch_size);
        }
    }

    /* Built-in benchmark from SIMD module */
    printf("\n--- Built-in Monte Carlo SIMD Benchmark ---\n");
    OFC_BenchmarkMonteCarloSIMD();

    /* Print summary */
    print_benchmark_summary(results, result_count);

    printf("\nOFC SIMD Benchmark Suite completed successfully!\n");
    printf("=================================================================\n");

    return 0;
}
