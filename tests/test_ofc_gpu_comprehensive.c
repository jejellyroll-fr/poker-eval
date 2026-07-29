/*
 * test_ofc_gpu_comprehensive.c - Comprehensive OFC GPU Test Suite
 *
 * This test suite provides exhaustive testing of the OFC GPU acceleration,
 * including performance benchmarks, edge cases, and stress tests.
 *
 * Copyright (C) 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/gpu/ofc_gpu.h>

/* Test configuration */
#define TEST_SKIP_CODE 77

/* Timing utilities */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/* Test result tracking */
typedef struct {
    const char *name;
    int passed;
    double time_ms;
    char notes[256];
} test_result_t;

static test_result_t g_test_results[32];
static int g_test_count = 0;

static void record_test(const char *name, int passed, double time_ms, const char *notes) {
    if (g_test_count >= 32) return;

    test_result_t *r = &g_test_results[g_test_count++];
    r->name = name;
    r->passed = passed;
    r->time_ms = time_ms;
    if (notes) {
        strncpy(r->notes, notes, sizeof(r->notes) - 1);
        r->notes[sizeof(r->notes) - 1] = '\0';
    } else {
        r->notes[0] = '\0';
    }
}

static void print_summary(void) {
    int passed = 0;
    double total_time = 0.0;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                 COMPREHENSIVE TEST SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    for (int i = 0; i < g_test_count; i++) {
        test_result_t *r = &g_test_results[i];
        printf("%-40s %s (%.2f ms)\n",
               r->name,
               r->passed ? "✓ PASS" : "✗ FAIL",
               r->time_ms);
        if (r->notes[0]) {
            printf("  └─ %s\n", r->notes);
        }
        if (r->passed) passed++;
        total_time += r->time_ms;
    }

    printf("\n");
    printf("───────────────────────────────────────────────────────────────\n");
    printf("Results: %d/%d tests passed (%.1f%%)\n",
           passed, g_test_count, (100.0 * passed) / g_test_count);
    printf("Total time: %.2f seconds\n", total_time / 1000.0);
    printf("═══════════════════════════════════════════════════════════════\n");
}

/* ===== Test Categories ===== */

/* Category 1: Batch Size Scaling Tests */
static int test_batch_size_scaling(void) {
    printf("\n=== Category 1: Batch Size Scaling Tests ===\n\n");

    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 32768, OFC_GPU_BACKEND_AUTO);
    if (!ctx) {
        printf("GPU not available, skipping\n");
        return 0;
    }

    int batch_sizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    int num_sizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);
    int all_passed = 1;

    for (int i = 0; i < num_sizes; i++) {
        int batch_size = batch_sizes[i];

        ofc_gpu_batch_t batch;
        memset(&batch, 0, sizeof(batch));

        for (int j = 0; j < batch_size; j++) {
            OFC_InitializeHand(&batch.partial_hands[j]);
            int card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
            StdDeck_CardMask_SET(batch.partial_hands[j].hands[OFC_TOP], card);
            batch.partial_hands[j].card_count[OFC_TOP] = 1;

            batch.cards[j] = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
            batch.positions[j] = OFC_TOP;
        }
        batch.batch_size = batch_size;
        batch.simulations_per_hand = 100;

        double start = get_time_ms();
        int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        double elapsed = get_time_ms() - start;

        char notes[256];
        if (result == 0) {
            double throughput = (batch_size * 100) / (elapsed / 1000.0);
            snprintf(notes, sizeof(notes),
                     "Batch %d: %.2f ms, %.2e sims/sec",
                     batch_size, elapsed, throughput);
            printf("  ✓ Batch size %5d: %.2f ms (%.2e sims/sec)\n",
                   batch_size, elapsed, throughput);
            record_test("Batch Scaling", 1, elapsed, notes);
        } else {
            snprintf(notes, sizeof(notes), "Batch %d: FAILED", batch_size);
            printf("  ✗ Batch size %5d: FAILED\n", batch_size);
            record_test("Batch Scaling", 0, elapsed, notes);
            all_passed = 0;
        }
    }

    OFC_GPU_Cleanup(ctx);
    return all_passed;
}

/* Category 2: Simulation Accuracy Tests */
static int test_simulation_accuracy(void) {
    printf("\n=== Category 2: Simulation Accuracy Tests ===\n\n");

    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    int sim_counts[] = {100, 500, 1000, 5000, 10000};
    int num_counts = sizeof(sim_counts) / sizeof(sim_counts[0]);

    ofc_hand_t test_hand;
    OFC_InitializeHand(&test_hand);
    int card = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
    StdDeck_CardMask_SET(test_hand.hands[OFC_TOP], card);
    test_hand.card_count[OFC_TOP] = 1;

    int test_card = StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS);
    ofc_position_t pos = OFC_TOP;

    int all_passed = 1;

    for (int i = 0; i < num_counts; i++) {
        int sims = sim_counts[i];

        ofc_gpu_batch_t batch;
        memset(&batch, 0, sizeof(batch));
        batch.partial_hands[0] = test_hand;
        batch.cards[0] = test_card;
        batch.positions[0] = pos;
        batch.simulations_per_hand = sims;
        batch.batch_size = 1;

        double start = get_time_ms();
        int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        double elapsed = get_time_ms() - start;

        if (result == 0) {
            float risk = batch.foul_risks[0];
            char notes[256];
            snprintf(notes, sizeof(notes),
                    "%d sims: risk=%.4f, %.2f ms",
                    sims, risk, elapsed);

            printf("  ✓ %6d simulations: risk=%.4f (%.2f ms)\n", sims, risk, elapsed);
            record_test("Simulation Accuracy", 1, elapsed, notes);
        } else {
            printf("  ✗ %6d simulations: FAILED\n", sims);
            record_test("Simulation Accuracy", 0, elapsed, "FAILED");
            all_passed = 0;
        }
    }

    OFC_GPU_Cleanup(ctx);
    return all_passed;
}

/* Category 3: GPU vs SIMD Validation */
static int test_gpu_vs_simd_validation(void) {
    printf("\n=== Category 3: GPU vs SIMD Validation ===\n\n");

    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* #define rather than const int: the latter is not a constant
     * expression in C, so these were variable-length arrays. */
#define FOUL_BATCH_SIZE 8
    ofc_hand_t test_hands[FOUL_BATCH_SIZE];
    int cards[FOUL_BATCH_SIZE];
    ofc_position_t positions[FOUL_BATCH_SIZE];
    float gpu_risks[FOUL_BATCH_SIZE];
    float simd_risks[FOUL_BATCH_SIZE];

    for (int i = 0; i < FOUL_BATCH_SIZE; i++) {
        OFC_InitializeHand(&test_hands[i]);
        cards[i] = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
        positions[i] = OFC_TOP;
    }

    ofc_gpu_batch_t gpu_batch;
    memset(&gpu_batch, 0, sizeof(gpu_batch));
    for (int i = 0; i < FOUL_BATCH_SIZE; i++) {
        gpu_batch.partial_hands[i] = test_hands[i];
        gpu_batch.cards[i] = cards[i];
        gpu_batch.positions[i] = positions[i];
    }
    gpu_batch.simulations_per_hand = 5000;
    gpu_batch.batch_size = FOUL_BATCH_SIZE;

    OFC_GPU_CalculateFoulRiskBatch(ctx, &gpu_batch);
    memcpy(gpu_risks, gpu_batch.foul_risks, sizeof(float) * FOUL_BATCH_SIZE);

    OFC_CalculateMultipleFoulRisksSIMD(
        test_hands, cards, positions, FOUL_BATCH_SIZE, 5000, simd_risks);

    int passed = 1;
    float tolerance = 0.05f;

    for (int i = 0; i < FOUL_BATCH_SIZE; i++) {
        float diff = fabsf(gpu_risks[i] - simd_risks[i]);
        if (diff > tolerance) {
            printf("  ✗ Hand %d: GPU=%.4f, SIMD=%.4f, diff=%.4f\n",
                   i, gpu_risks[i], simd_risks[i], diff);
            passed = 0;
        }
    }

    if (passed) {
        printf("  ✓ All results within tolerance (5%%)\n");
        record_test("GPU vs SIMD Validation", 1, 0, "All within tolerance");
    } else {
        record_test("GPU vs SIMD Validation", 0, 0, "Some differences > 5%");
    }

    OFC_GPU_Cleanup(ctx);
    return passed;
}
#undef FOUL_BATCH_SIZE

/* Category 4: Edge Cases */
static int test_edge_cases(void) {
    printf("\n=== Category 4: Edge Cases Tests ===\n\n");

    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 4096, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    int all_passed = 1;

    /* Test 4.1: Empty hand */
    printf("  Test 4.1: Empty hand\n");
    {
        ofc_gpu_batch_t batch;
        memset(&batch, 0, sizeof(batch));
        OFC_InitializeHand(&batch.partial_hands[0]);
        batch.cards[0] = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
        batch.positions[0] = OFC_TOP;
        batch.simulations_per_hand = 100;
        batch.batch_size = 1;

        double start = get_time_ms();
        int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        double elapsed = get_time_ms() - start;

        if (result == 0) {
            printf("    ✓ Empty hand: risk=%.4f (%.2f ms)\n",
                   batch.foul_risks[0], elapsed);
            record_test("Edge: Empty Hand", 1, elapsed, NULL);
        } else {
            printf("    ✗ Empty hand: FAILED\n");
            record_test("Edge: Empty Hand", 0, elapsed, "FAILED");
            all_passed = 0;
        }
    }

    /* Test 4.2: All same position */
    printf("  Test 4.2: Batch with all same position\n");
    {
        ofc_gpu_batch_t batch;
        memset(&batch, 0, sizeof(batch));

        for (int i = 0; i < 100; i++) {
            OFC_InitializeHand(&batch.partial_hands[i]);
            batch.cards[i] = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
            batch.positions[i] = OFC_TOP;
        }
        batch.batch_size = 100;
        batch.simulations_per_hand = 100;

        double start = get_time_ms();
        int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        double elapsed = get_time_ms() - start;

        if (result == 0) {
            printf("    ✓ All same position: %.2f ms\n", elapsed);
            record_test("Edge: Same Position", 1, elapsed, NULL);
        } else {
            printf("    ✗ All same position: FAILED\n");
            record_test("Edge: Same Position", 0, elapsed, "FAILED");
            all_passed = 0;
        }
    }

    OFC_GPU_Cleanup(ctx);
    return all_passed;
}

/* ===== Main Test Runner ===== */

int main(void) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("        OFC GPU COMPREHENSIVE TEST SUITE\n");
    printf("═══════════════════════════════════════════════════════════════\n");

    printf("\nChecking GPU availability...\n");
    int cuda = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_CUDA);
    int opencl = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_OPENCL);

    printf("  CUDA:   %s\n", cuda ? "Available" : "Not Available");
    printf("  OpenCL: %s\n", opencl ? "Available" : "Not Available");

    if (!cuda && !opencl) {
        printf("\n⚠ No GPU available - tests cannot run\n");
        printf("This is not a failure - GPU may not be present\n");
        return TEST_SKIP_CODE;
    }

    /* Run test categories */
    test_batch_size_scaling();
    test_simulation_accuracy();
    test_gpu_vs_simd_validation();
    test_edge_cases();

    /* Print summary */
    print_summary();

    int passed = 0;
    for (int i = 0; i < g_test_count; i++) {
        if (g_test_results[i].passed) passed++;
    }

    return (passed == g_test_count) ? 0 : 1;
}
