/*
 * test_ofc_gpu_basic.c - Basic OFC GPU functionality tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h>
#include <poker_eval/gpu/ofc_gpu.h>

/* Test configuration */
#define TEST_SKIP_CODE 77

/* Test helper functions */
static void init_test_hand(ofc_hand_t *hand);
static int compare_foul_risks(float gpu_risk, float cpu_risk, float tolerance);
static void print_test_result(const char *test_name, int passed);

/* Test cases */
static int test_gpu_availability(void);
static int test_gpu_context_init(void);
static int test_gpu_memory_allocation(void);
static int test_gpu_single_hand_foul_risk(void);
static int test_gpu_batch_processing(void);
static int test_gpu_vs_simd_validation(void);
static int test_gpu_error_handling(void);

int main(void) {
    int total = 0, passed = 0;

    printf("====================================\n");
    printf("  OFC GPU Basic Tests\n");
    printf("====================================\n\n");

    /* Test 1: GPU Availability */
    printf("Test 1: GPU Availability\n");
    if (test_gpu_availability()) {
        passed++;
        printf("  ✓ GPU is available\n\n");
    } else {
        printf("  ⚠ GPU not available - tests will be skipped\n");
        printf("  This is not a failure - system may not have GPU\n\n");
        return TEST_SKIP_CODE;  /* Exit gracefully if no GPU */
    }
    total++;

    /* Test 2: Context Initialization */
    printf("Test 2: GPU Context Initialization\n");
    if (test_gpu_context_init()) {
        passed++;
        printf("  ✓ Context initialized successfully\n\n");
    } else {
        printf("  ✗ Context initialization failed\n\n");
        /* Don't continue if we can't initialize */
        goto summary;
    }
    total++;

    /* Test 3: Memory Allocation */
    printf("Test 3: GPU Memory Allocation\n");
    if (test_gpu_memory_allocation()) {
        passed++;
        printf("  ✓ Memory allocation successful\n\n");
    } else {
        printf("  ✗ Memory allocation failed\n\n");
    }
    total++;

    /* Test 4: Single Hand Foul Risk */
    printf("Test 4: Single Hand Foul Risk\n");
    if (test_gpu_single_hand_foul_risk()) {
        passed++;
        printf("  ✓ Single hand processing works\n\n");
    } else {
        printf("  ✗ Single hand processing failed\n\n");
    }
    total++;

    /* Test 5: Batch Processing */
    printf("Test 5: Batch Processing\n");
    if (test_gpu_batch_processing()) {
        passed++;
        printf("  ✓ Batch processing works\n\n");
    } else {
        printf("  ✗ Batch processing failed\n\n");
    }
    total++;


    /* Test 7: GPU vs SIMD Validation */
    printf("Test 7: GPU vs SIMD Validation\n");
    if (test_gpu_vs_simd_validation()) {
        passed++;
        printf("  ✓ Results match SIMD reference\n\n");
    } else {
        printf("  ✗ Results don't match SIMD\n\n");
    }
    total++;

    /* Test 8: Error Handling */
    printf("Test 8: Error Handling\n");
    if (test_gpu_error_handling()) {
        passed++;
        printf("  ✓ Error handling works\n\n");
    } else {
        printf("  ✗ Error handling failed\n\n");
    }
    total++;

summary:
    printf("====================================\n");
    printf("Results: %d/%d tests passed (%.1f%%)\n",
           passed, total, (100.0 * passed) / total);
    printf("====================================\n");

    return (passed == total) ? 0 : 1;
}

/* Test implementations */

static int test_gpu_availability(void) {
    /* Test both CUDA and OpenCL */
    int cuda_available = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_CUDA);
    int opencl_available = OFC_GPU_IsAvailable(OFC_GPU_BACKEND_OPENCL);

    printf("  CUDA available: %s\n", cuda_available ? "YES" : "NO");
    printf("  OpenCL available: %s\n", opencl_available ? "YES" : "NO");

    return (cuda_available || opencl_available);
}

static int test_gpu_context_init(void) {
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* Test optimal batch size */
    size_t optimal_batch = OFC_GPU_GetOptimalBatchSize(ctx);
    printf("  Optimal batch size: %zu\n", optimal_batch);

    /* Cleanup */
    OFC_GPU_Cleanup(ctx);
    return 1;
}

static int test_gpu_memory_allocation(void) {
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 4096, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* Check allocated memory */
    printf("  Allocated memory: %.2f MB\n",
           (double)ctx->allocated_memory_size / (1024.0 * 1024.0));

    int result = (ctx->allocated_memory_size > 0);
    OFC_GPU_Cleanup(ctx);
    return result;
}

static int test_gpu_single_hand_foul_risk(void) {
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* Create test hand */
    ofc_hand_t test_hand;
    OFC_InitializeHand(&test_hand);

    /* Add some cards to top */
    int cards[] = {
        StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES),
        StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS)
    };

    for (int i = 0; i < 2; i++) {
        StdDeck_CardMask_SET(test_hand.hands[OFC_TOP], cards[i]);
        test_hand.card_count[OFC_TOP]++;
    }

    /* Create batch with single hand */
    ofc_gpu_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.partial_hands[0] = test_hand;
    batch.cards[0] = StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_DIAMONDS);
    batch.positions[0] = OFC_TOP;
    batch.simulations_per_hand = 1000;
    batch.batch_size = 1;

    /* Process */
    int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);

    if (result == 0) {
        printf("  Foul risk: %.4f (1000 simulations)\n", batch.foul_risks[0]);
        printf("  Processing time: %.2f ms\n", batch.processing_time_ms);
    }

    OFC_GPU_Cleanup(ctx);
    return (result == 0);
}

static int test_gpu_batch_processing(void) {
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* Create batch with multiple hands */
    ofc_gpu_batch_t batch;
    memset(&batch, 0, sizeof(batch));

    int batch_sizes[] = {4, 8, 16, 32};
    int passed = 1;

    for (int i = 0; i < 4; i++) {
        int size = batch_sizes[i];
        batch.batch_size = size;
        batch.simulations_per_hand = 100;

        /* Initialize test hands */
        for (int j = 0; j < size; j++) {
            OFC_InitializeHand(&batch.partial_hands[j]);
            batch.cards[j] = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
            batch.positions[j] = OFC_TOP;
        }

        /* Process */
        int result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);

        if (result != 0) {
            printf("  ✗ Batch size %d failed\n", size);
            passed = 0;
        } else {
            printf("  ✓ Batch size %d: %.2f ms\n", size, batch.processing_time_ms);
        }
    }

    OFC_GPU_Cleanup(ctx);
    return passed;
}


static int test_gpu_vs_simd_validation(void) {
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (!ctx) return 0;

    /* Create test batch. #define rather than const int: in C the latter is
     * not a constant expression, so the arrays below were variable-length
     * arrays, which clang rejects here. */
#define batch_size 8
    ofc_hand_t test_hands[batch_size];
    int cards[batch_size];
    ofc_position_t positions[batch_size];
    float gpu_risks[batch_size];
    float simd_risks[batch_size];

    /* Initialize */
    for (int i = 0; i < batch_size; i++) {
        OFC_InitializeHand(&test_hands[i]);
        cards[i] = StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES);
        positions[i] = OFC_TOP;
    }

    /* Calculate with GPU */
    ofc_gpu_batch_t gpu_batch;
    memset(&gpu_batch, 0, sizeof(gpu_batch));
    for (int i = 0; i < batch_size; i++) {
        gpu_batch.partial_hands[i] = test_hands[i];
        gpu_batch.cards[i] = cards[i];
        gpu_batch.positions[i] = positions[i];
    }
    gpu_batch.simulations_per_hand = 5000;
    gpu_batch.batch_size = batch_size;

    OFC_GPU_CalculateFoulRiskBatch(ctx, &gpu_batch);
    memcpy(gpu_risks, gpu_batch.foul_risks, sizeof(float) * batch_size);

    /* Calculate with SIMD */
    OFC_CalculateMultipleFoulRisksSIMD(
        test_hands, cards, positions, batch_size, 5000, simd_risks);

    /* Compare */
    int passed = 1;
    float tolerance = 0.05f;

    for (int i = 0; i < batch_size; i++) {
        float diff = fabsf(gpu_risks[i] - simd_risks[i]);
        if (diff > tolerance) {
            printf("  ✗ Hand %d: GPU=%.4f, SIMD=%.4f, diff=%.4f\n",
                   i, gpu_risks[i], simd_risks[i], diff);
            passed = 0;
        }
    }

    if (passed) {
        printf("  ✓ All results within tolerance\n");
    }

    OFC_GPU_Cleanup(ctx);
    return passed;
}
#undef batch_size

static int test_gpu_error_handling(void) {
    int passed = 1;

    /* Test NULL context */
    ofc_gpu_batch_t batch;
    memset(&batch, 0, sizeof(batch));
    batch.batch_size = 1;

    int result = OFC_GPU_CalculateFoulRiskBatch(NULL, &batch);
    if (result != -1) {
        printf("  ✗ NULL context should return error\n");
        passed = 0;
    }

    /* Test invalid batch size */
    ofc_gpu_context_t *ctx = OFC_GPU_Init(-1, 1024, OFC_GPU_BACKEND_AUTO);
    if (ctx) {
        batch.batch_size = 0;
        result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        if (result != -1) {
            printf("  ✗ Invalid batch size should return error\n");
            passed = 0;
        }

        batch.batch_size = OFC_GPU_MAX_BATCH_SIZE + 1;
        result = OFC_GPU_CalculateFoulRiskBatch(ctx, &batch);
        if (result != -1) {
            printf("  ✗ Oversized batch should return error\n");
            passed = 0;
        }

        OFC_GPU_Cleanup(ctx);
    }

    if (passed) {
        printf("  ✓ Error handling works correctly\n");
    }

    return passed;
}

/* Helper functions */

static void init_test_hand(ofc_hand_t *hand) {
    OFC_InitializeHand(hand);
    /* Add some test cards */
}

static int compare_foul_risks(float gpu_risk, float cpu_risk, float tolerance) {
    return fabs(gpu_risk - cpu_risk) <= tolerance;
}

static void print_test_result(const char *test_name, int passed) {
    printf("%s: %s\n", test_name, passed ? "PASSED" : "FAILED");
}
