/*
 * test_simd_operations.c: Test suite for SIMD card operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <poker_eval/utils/simd_card_operations.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/eval.h>

#define NUM_TEST_HANDS 1000
#define BENCHMARK_ITERATIONS 100000

/* Generate a random 7-card hand */
static void generate_random_hand(StdDeck_CardMask *hand)
{
    int cards[7];
    int i, j;

    StdDeck_CardMask_RESET(*hand);

    /* Generate 7 unique random cards */
    for (i = 0; i < 7; i++)
    {
        int card;
        int unique;

        do
        {
            unique = 1;
            card = rand() % 52;

            /* Check if card already selected */
            for (j = 0; j < i; j++)
            {
                if (cards[j] == card)
                {
                    unique = 0;
                    break;
                }
            }
        } while (!unique);

        cards[i] = card;
        StdDeck_CardMask_SET(*hand, card);
    }
}

/* Test capability detection */
static void test_capability_detection(void)
{
    simd_capability_t cap;

    printf("=== SIMD Capability Detection Test ===\n");

    cap = simd_detect_capability();
    printf("Detected SIMD capability: %s\n", simd_capability_name(cap));

    /* Test all capability names */
    printf("\nAll capability names:\n");
    printf("  SIMD_NONE: %s\n", simd_capability_name(SIMD_NONE));
    printf("  SIMD_SSE2: %s\n", simd_capability_name(SIMD_SSE2));
    printf("  SIMD_AVX2: %s\n", simd_capability_name(SIMD_AVX2));
    printf("  SIMD_AVX512: %s\n", simd_capability_name(SIMD_AVX512));

    printf("\n");
}

/* Test batch preparation and extraction */
static void test_batch_operations(void)
{
    StdDeck_CardMask hands[8];
    simd_card_batch_t batch;
    simd_result_batch_t results;
    HandVal extracted[8];
    int i;

    printf("=== Batch Operations Test ===\n");

    /* Generate test hands */
    for (i = 0; i < 8; i++)
    {
        generate_random_hand(&hands[i]);
    }

    /* Test batch preparation */
    simd_prepare_batch_from_masks(hands, 8, &batch);
    printf("Batch prepared with %d hands\n", batch.batch_size);

    /* Verify batch data */
    for (i = 0; i < batch.batch_size; i++)
    {
        /* Reconstruct using the same structure */
        StdDeck_CardMask reconstructed;
        reconstructed.cards_n = 0;
        reconstructed.cards.spades = (uint16_t)(batch.spades[i] & 0x1FFF);
        reconstructed.cards.clubs = (uint16_t)(batch.clubs[i] & 0x1FFF);
        reconstructed.cards.diamonds = (uint16_t)(batch.diamonds[i] & 0x1FFF);
        reconstructed.cards.hearts = (uint16_t)(batch.hearts[i] & 0x1FFF);

        if (reconstructed.cards_n != hands[i].cards_n)
        {
            printf("ERROR: Batch preparation failed for hand %d\n", i);
            printf("  Original: 0x%llx\n", (unsigned long long)hands[i].cards_n);
            printf("  Reconstructed: 0x%llx\n", (unsigned long long)reconstructed.cards_n);
            printf("  Spades: orig=%u, batch=%u\n", hands[i].cards.spades, batch.spades[i]);
            printf("  Clubs: orig=%u, batch=%u\n", hands[i].cards.clubs, batch.clubs[i]);
            printf("  Diamonds: orig=%u, batch=%u\n", hands[i].cards.diamonds, batch.diamonds[i]);
            printf("  Hearts: orig=%u, batch=%u\n", hands[i].cards.hearts, batch.hearts[i]);
        }
    }

    /* Test result extraction */
    results.batch_size = 8;
    for (i = 0; i < 8; i++)
    {
        results.results[i] = i * 1000; /* Dummy values */
    }

    simd_extract_results_to_array(&results, extracted);

    /* Verify extraction */
    int extraction_ok = 1;
    for (i = 0; i < 8; i++)
    {
        if (extracted[i] != results.results[i])
        {
            printf("ERROR: Result extraction failed at index %d\n", i);
            extraction_ok = 0;
        }
    }

    if (extraction_ok)
    {
        printf("Result extraction: OK\n");
    }

    printf("\n");
}

/* Test correctness of SIMD evaluation */
static void test_correctness(void)
{
    StdDeck_CardMask test_hands[NUM_TEST_HANDS];
    HandVal scalar_results[NUM_TEST_HANDS];
    HandVal simd_results[NUM_TEST_HANDS];
    int i;
    int errors = 0;

    printf("=== Correctness Test ===\n");
    printf("Testing %d random hands...\n", NUM_TEST_HANDS);

    /* Generate test hands */
    for (i = 0; i < NUM_TEST_HANDS; i++)
    {
        generate_random_hand(&test_hands[i]);
    }

    /* Compute scalar results */
    for (i = 0; i < NUM_TEST_HANDS; i++)
    {
        scalar_results[i] = StdDeck_StdRules_EVAL_N(test_hands[i], 7);
    }

    /* Compute SIMD results */
    if (simd_eval_multiple_hands(test_hands, NUM_TEST_HANDS, simd_results) < 0)
    {
        printf("ERROR: SIMD evaluation failed\n");
        return;
    }

    /* Compare results */
    for (i = 0; i < NUM_TEST_HANDS; i++)
    {
        if (scalar_results[i] != simd_results[i])
        {
            errors++;
            printf("ERROR: Mismatch at hand %d\n", i);
            printf("  Scalar: %d\n", scalar_results[i]);
            printf("  SIMD: %d\n", simd_results[i]);

            /* Show first few errors only */
            if (errors >= 5)
            {
                printf("  ... (showing first 5 errors only)\n");
                break;
            }
        }
    }

    if (errors == 0)
    {
        printf("All %d hands evaluated correctly!\n", NUM_TEST_HANDS);
    }
    else
    {
        printf("Found %d errors in %d hands\n", errors, NUM_TEST_HANDS);
    }

    /* Use built-in validation */
    errors = simd_validate_against_scalar(test_hands, NUM_TEST_HANDS);
    printf("Built-in validation found %d errors\n", errors);

    printf("\n");
}

/* Benchmark different implementations */
static void test_performance(void)
{
    StdDeck_CardMask test_hands[SIMD_AVX512_BATCH_SIZE];
    simd_card_batch_t batch;
    simd_result_batch_t batch_results;
    clock_t start, end;
    double scalar_time, simd_time, speedup;
    int i, j;

    printf("=== Performance Test ===\n");
    printf("Benchmarking with %d iterations...\n", BENCHMARK_ITERATIONS);

    /* Generate test hands */
    for (i = 0; i < SIMD_AVX512_BATCH_SIZE; i++)
    {
        generate_random_hand(&test_hands[i]);
    }

    /* Prepare batch for SIMD */
    simd_prepare_batch_from_masks(test_hands, SIMD_AVX512_BATCH_SIZE, &batch);

    /* Benchmark scalar evaluation */
    start = clock();
    for (i = 0; i < BENCHMARK_ITERATIONS; i++)
    {
        for (j = 0; j < SIMD_AVX512_BATCH_SIZE; j++)
        {
            HandVal result = StdDeck_StdRules_EVAL_N(test_hands[j], 7);
            (void)result; // Suppress unused variable warning
        }
    }
    end = clock();
    scalar_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    /* Benchmark SIMD evaluation */
    start = clock();
    for (i = 0; i < BENCHMARK_ITERATIONS; i++)
    {
        simd_eval_batch_hands_adaptive(&batch, &batch_results);
    }
    end = clock();
    simd_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    /* Calculate speedup */
    speedup = scalar_time / simd_time;

    printf("\nResults:\n");
    printf("  Scalar time: %.3f seconds\n", scalar_time);
    printf("  SIMD time: %.3f seconds\n", simd_time);
    printf("  Speedup: %.2fx\n", speedup);

    /* Benchmark individual capabilities if available */
    simd_capability_t cap = simd_detect_capability();

    if (cap >= SIMD_AVX2)
    {
        double avx2_time = simd_benchmark_capability(SIMD_AVX2, BENCHMARK_ITERATIONS);
        printf("\n  AVX2 time: %.3f seconds (%.2fx speedup)\n",
               avx2_time, scalar_time / avx2_time);
    }

    if (cap >= SIMD_AVX512)
    {
        double avx512_time = simd_benchmark_capability(SIMD_AVX512, BENCHMARK_ITERATIONS);
        printf("  AVX-512 time: %.3f seconds (%.2fx speedup)\n",
               avx512_time, scalar_time / avx512_time);
    }

    printf("\n");
}

/* Test different batch sizes */
static void test_batch_sizes(void)
{
    StdDeck_CardMask hands[100];
    HandVal results[100];
    int batch_sizes[] = {1, 4, 7, 8, 16, 32, 64, 100};
    int i, j;

    printf("=== Batch Size Test ===\n");

    /* Generate test hands */
    for (i = 0; i < 100; i++)
    {
        generate_random_hand(&hands[i]);
    }

    /* Test different batch sizes */
    for (i = 0; i < sizeof(batch_sizes) / sizeof(batch_sizes[0]); i++)
    {
        int size = batch_sizes[i];
        clock_t start = clock();

        if (simd_eval_multiple_hands(hands, size, results) < 0)
        {
            printf("Batch size %d: FAILED\n", size);
            continue;
        }

        clock_t end = clock();
        double time = ((double)(end - start)) / CLOCKS_PER_SEC * 1000; /* ms */

        /* Verify first few results */
        int correct = 1;
        for (j = 0; j < size && j < 5; j++)
        {
            HandVal expected = StdDeck_StdRules_EVAL_N(hands[j], 7);
            if (results[j] != expected)
            {
                correct = 0;
                break;
            }
        }

        printf("Batch size %3d: %s (%.3f ms)\n",
               size, correct ? "OK" : "FAILED", time);
    }

    printf("\n");
}

int main(int argc, char *argv[])
{
    /* Initialize random seed */
    srand((unsigned int)time(NULL));

    printf("SIMD Card Operations Test Suite\n");
    printf("===============================\n\n");

    /* Run all tests */
    test_capability_detection();
    test_batch_operations();
    test_correctness();
    test_performance();
    test_batch_sizes();

    printf("All tests completed.\n");

    return 0;
}
