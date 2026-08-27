/*
 * test_gpu_multigame.c: Comprehensive GPU multi-game parity tests
 *
 * Tests batched GPU evaluation for Stud, Razz, Hi/Lo, and Omaha8
 * by comparing GPU results against CPU evaluation.
 *
 * All tests gate on GPU availability and return TEST_SKIP_CODE (77)
 * when no GPU hardware is present.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/gpu/eval_batched_gpu.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/simd_operations.h>
#include "gpu_table_loader.h"
#include <poker_eval/games/eval_low.h>
#include "unity.h"

#define TEST_SKIP_CODE 77

/* Batch sizes for testing */
#define SMALL_BATCH  100
#define MEDIUM_BATCH 1000
#define LARGE_BATCH  10000

static gpu_eval_context_t* g_ctx = NULL;
static int g_gpu_available = 0;

/* ===== Helpers ===== */

/* Generate a random card [0,51] not in dead mask */
static int random_card(StdDeck_CardMask* dead) {
    int card;
    do {
        card = rand() % 52;
    } while (StdDeck_CardMask_CARD_IS_SET(*dead, card));
    StdDeck_CardMask_SET(*dead, card);
    return card;
}

/* Generate random 7-card hand as uint8_t array */
static void random_7card_hand(uint8_t out[7]) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    for (int i = 0; i < 7; i++) {
        out[i] = (uint8_t)random_card(&dead);
    }
}

/* Generate random 9-card Omaha hand (4 hole + 5 board) as uint8_t array */
static void random_omaha_hand(uint8_t out[9]) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    for (int i = 0; i < 9; i++) {
        out[i] = (uint8_t)random_card(&dead);
    }
}

/* CPU evaluation of 7-card hand (best of C(7,5)=21 combos) */
static uint32_t cpu_eval_7card_high(const uint8_t cards[7]) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < 7; i++) {
        StdDeck_CardMask_SET(hand, cards[i]);
    }
    return (uint32_t)StdDeck_StdRules_EVAL_N(hand, 7);
}

/* CPU low evaluation of 7-card hand (A-5 lowball) */
static uint32_t cpu_eval_7card_low(const uint8_t cards[7]) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < 7; i++) {
        StdDeck_CardMask_SET(hand, cards[i]);
    }
    return (uint32_t)StdDeck_Lowball_EVAL(hand, 7);
}

/* CPU Omaha high evaluation (4 hole + 5 board, must use 2+3) */
static uint32_t cpu_eval_omaha_high(const uint8_t cards[9]) {
    StdDeck_CardMask hole, board;
    StdDeck_CardMask_RESET(hole);
    StdDeck_CardMask_RESET(board);
    for (int i = 0; i < 4; i++) {
        StdDeck_CardMask_SET(hole, cards[i]);
    }
    for (int i = 4; i < 9; i++) {
        StdDeck_CardMask_SET(board, cards[i]);
    }

    /* Enumerate C(4,2)*C(5,3) = 60 combos */
    static const int hole_combos[6][2] = {
        {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
    };
    static const int board_combos[10][3] = {
        {0,1,2}, {0,1,3}, {0,1,4}, {0,2,3}, {0,2,4},
        {0,3,4}, {1,2,3}, {1,2,4}, {1,3,4}, {2,3,4}
    };

    uint32_t best = 0;
    for (int h = 0; h < 6; h++) {
        for (int b = 0; b < 10; b++) {
            StdDeck_CardMask hand5;
            StdDeck_CardMask_RESET(hand5);
            StdDeck_CardMask_SET(hand5, cards[hole_combos[h][0]]);
            StdDeck_CardMask_SET(hand5, cards[hole_combos[h][1]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][0]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][1]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][2]]);

            uint32_t val = (uint32_t)StdDeck_StdRules_EVAL_N(hand5, 5);
            if (val > best) best = val;
        }
    }
    return best;
}

/* CPU Omaha low evaluation (4 hole + 5 board, A-5, 8-or-better) */
static uint32_t cpu_eval_omaha_low(const uint8_t cards[9]) {
    static const int hole_combos[6][2] = {
        {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
    };
    static const int board_combos[10][3] = {
        {0,1,2}, {0,1,3}, {0,1,4}, {0,2,3}, {0,2,4},
        {0,3,4}, {1,2,3}, {1,2,4}, {1,3,4}, {2,3,4}
    };

    uint32_t best_lo = LowHandVal_NOTHING;
    for (int h = 0; h < 6; h++) {
        for (int b = 0; b < 10; b++) {
            StdDeck_CardMask hand5;
            StdDeck_CardMask_RESET(hand5);
            StdDeck_CardMask_SET(hand5, cards[hole_combos[h][0]]);
            StdDeck_CardMask_SET(hand5, cards[hole_combos[h][1]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][0]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][1]]);
            StdDeck_CardMask_SET(hand5, cards[4 + board_combos[b][2]]);

            uint32_t lo = (uint32_t)StdDeck_Lowball_EVAL(hand5, 5);
            if (lo < best_lo) best_lo = lo;
        }
    }
    return best_lo;
}

/* ===== Unity Setup/Teardown ===== */

void setUp(void) {
    /* Nothing per-test */
}

void tearDown(void) {
    /* Nothing per-test */
}

/* ===== Test: GPU Stud batch vs CPU ===== */

void test_gpu_stud_batch_vs_cpu(void) {
    if (!g_gpu_available) {
        TEST_IGNORE_MESSAGE("No GPU available — skipping");
        return;
    }

    const int N = MEDIUM_BATCH;
    uint8_t* hands = (uint8_t*)malloc(N * 7);
    uint32_t* gpu_vals = (uint32_t*)malloc(N * sizeof(uint32_t));
    TEST_ASSERT_NOT_NULL(hands);
    TEST_ASSERT_NOT_NULL(gpu_vals);

    for (int i = 0; i < N; i++) {
        random_7card_hand(&hands[i * 7]);
    }

    int rc = gpu_eval_stud_batch(g_ctx, hands, N, gpu_vals);
    TEST_ASSERT_EQUAL_INT(0, rc);

    int mismatches = 0;
    for (int i = 0; i < N; i++) {
        uint32_t cpu_val = cpu_eval_7card_high(&hands[i * 7]);
        if (gpu_vals[i] != cpu_val) {
            mismatches++;
            if (mismatches <= 5) {
                printf("  Stud mismatch at %d: GPU=%u CPU=%u\n", i, gpu_vals[i], cpu_val);
            }
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mismatches, "Stud GPU vs CPU mismatch count");

    free(hands);
    free(gpu_vals);
}

/* ===== Test: GPU Razz batch vs CPU ===== */

void test_gpu_razz_batch_vs_cpu(void) {
    if (!g_gpu_available) {
        TEST_IGNORE_MESSAGE("No GPU available — skipping");
        return;
    }

    const int N = MEDIUM_BATCH;
    uint8_t* hands = (uint8_t*)malloc(N * 7);
    uint32_t* gpu_vals = (uint32_t*)malloc(N * sizeof(uint32_t));
    TEST_ASSERT_NOT_NULL(hands);
    TEST_ASSERT_NOT_NULL(gpu_vals);

    for (int i = 0; i < N; i++) {
        random_7card_hand(&hands[i * 7]);
    }

    int rc = gpu_eval_razz_batch(g_ctx, hands, N, gpu_vals);
    TEST_ASSERT_EQUAL_INT(0, rc);

    int mismatches = 0;
    for (int i = 0; i < N; i++) {
        uint32_t cpu_val = cpu_eval_7card_low(&hands[i * 7]);
        if (gpu_vals[i] != cpu_val) {
            mismatches++;
            if (mismatches <= 5) {
                printf("  Razz mismatch at %d: GPU=%u CPU=%u\n", i, gpu_vals[i], cpu_val);
            }
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mismatches, "Razz GPU vs CPU mismatch count");

    free(hands);
    free(gpu_vals);
}

/* ===== Test: GPU Omaha Hi/Lo vs CPU ===== */

void test_gpu_omaha_hilo_vs_cpu(void) {
    if (!g_gpu_available) {
        TEST_IGNORE_MESSAGE("No GPU available — skipping");
        return;
    }

    const int N = SMALL_BATCH;
    uint8_t* hands = (uint8_t*)malloc(N * 9);
    uint32_t* gpu_hi = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* gpu_lo = (uint32_t*)malloc(N * sizeof(uint32_t));
    int* gpu_qual = (int*)malloc(N * sizeof(int));
    TEST_ASSERT_NOT_NULL(hands);
    TEST_ASSERT_NOT_NULL(gpu_hi);
    TEST_ASSERT_NOT_NULL(gpu_lo);
    TEST_ASSERT_NOT_NULL(gpu_qual);

    for (int i = 0; i < N; i++) {
        random_omaha_hand(&hands[i * 9]);
    }

    int rc = gpu_eval_omaha_hilo_batch(g_ctx, hands, N, gpu_hi, gpu_lo, gpu_qual);
    TEST_ASSERT_EQUAL_INT(0, rc);

    int hi_mismatches = 0, lo_mismatches = 0;
    for (int i = 0; i < N; i++) {
        uint32_t cpu_hi = cpu_eval_omaha_high(&hands[i * 9]);
        uint32_t cpu_lo = cpu_eval_omaha_low(&hands[i * 9]);

        if (gpu_hi[i] != cpu_hi) {
            hi_mismatches++;
            if (hi_mismatches <= 3) {
                printf("  OmahaHiLo HI mismatch at %d: GPU=%u CPU=%u\n", i, gpu_hi[i], cpu_hi);
            }
        }
        if (gpu_lo[i] != cpu_lo) {
            lo_mismatches++;
            if (lo_mismatches <= 3) {
                printf("  OmahaHiLo LO mismatch at %d: GPU=%u CPU=%u\n", i, gpu_lo[i], cpu_lo);
            }
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, hi_mismatches, "Omaha Hi/Lo HI mismatch count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lo_mismatches, "Omaha Hi/Lo LO mismatch count");

    free(hands);
    free(gpu_hi);
    free(gpu_lo);
    free(gpu_qual);
}

/* ===== Test: GPU generic Hi/Lo (Stud8) vs CPU ===== */

void test_gpu_stud_hilo_vs_cpu(void) {
    if (!g_gpu_available) {
        TEST_IGNORE_MESSAGE("No GPU available — skipping");
        return;
    }

    const int N = SMALL_BATCH;
    uint8_t* hands = (uint8_t*)malloc(N * 7);
    uint32_t* gpu_hi = (uint32_t*)malloc(N * sizeof(uint32_t));
    uint32_t* gpu_lo = (uint32_t*)malloc(N * sizeof(uint32_t));
    int* gpu_qual = (int*)malloc(N * sizeof(int));
    TEST_ASSERT_NOT_NULL(hands);

    for (int i = 0; i < N; i++) {
        random_7card_hand(&hands[i * 7]);
    }

    int rc = gpu_eval_hilo_batch(g_ctx, hands, N, 7, 0 /* game_type unused */,
                                  gpu_hi, gpu_lo, gpu_qual);
    TEST_ASSERT_EQUAL_INT(0, rc);

    int hi_mismatches = 0, lo_mismatches = 0;
    for (int i = 0; i < N; i++) {
        uint32_t cpu_hi = cpu_eval_7card_high(&hands[i * 7]);
        uint32_t cpu_lo = cpu_eval_7card_low(&hands[i * 7]);

        if (gpu_hi[i] != cpu_hi) hi_mismatches++;
        if (gpu_lo[i] != cpu_lo) lo_mismatches++;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, hi_mismatches, "Stud8 HI mismatch count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lo_mismatches, "Stud8 LO mismatch count");

    free(hands);
    free(gpu_hi);
    free(gpu_lo);
    free(gpu_qual);
}

/* ===== Test: Low table loading ===== */

void test_gpu_low_table_loading(void) {
    gpu_low_lookup_tables_t tables;
    int rc = gpu_load_low_tables(&tables);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = gpu_validate_low_tables(&tables);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* bottomFiveCardsTable[0] should be 0 (no bits set) */
    TEST_ASSERT_EQUAL_UINT32(0, tables.bottom_five_table[0]);

    /* A non-zero entry: 5 lowest ranks set = bits 0-4 = 0x1F = 31 */
    /* bottomFiveCardsTable[31] should encode ranks 0,1,2,3,4 */
    TEST_ASSERT_NOT_EQUAL(0, tables.bottom_five_table[31]);
}

/* ===== Test: Batch size scaling ===== */

void test_gpu_batch_size_scaling(void) {
    if (!g_gpu_available) {
        TEST_IGNORE_MESSAGE("No GPU available — skipping");
        return;
    }

    int batch_sizes[] = { 1, 10, 100, 1000, 10000 };
    int num_sizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    for (int s = 0; s < num_sizes; s++) {
        int N = batch_sizes[s];
        uint8_t* hands = (uint8_t*)malloc(N * 7);
        uint32_t* vals = (uint32_t*)malloc(N * sizeof(uint32_t));
        TEST_ASSERT_NOT_NULL(hands);
        TEST_ASSERT_NOT_NULL(vals);

        for (int i = 0; i < N; i++) {
            random_7card_hand(&hands[i * 7]);
        }

        int rc = gpu_eval_stud_batch(g_ctx, hands, N, vals);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "Stud batch should succeed");

        /* Spot-check first hand */
        uint32_t cpu_val = cpu_eval_7card_high(&hands[0]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(cpu_val, vals[0],
            "First hand should match CPU");

        free(hands);
        free(vals);
    }
}

/* ===== Test: SIMD RNG ===== */

void test_simd_rng_basic(void) {
    simd_rng_state_t rng;
    simd_rng_init(&rng, 42);

    uint32_t out[8];
    simd_rng_next_8(&rng, out);

    /* All 8 values should be different (decorrelated lanes) */
    int all_different = 1;
    for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            if (out[i] == out[j]) {
                all_different = 0;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(all_different, "SIMD RNG lanes should produce different values");

    /* Multiple calls should produce different sequences */
    uint32_t out2[8];
    simd_rng_next_8(&rng, out2);
    int any_different = 0;
    for (int i = 0; i < 8; i++) {
        if (out[i] != out2[i]) {
            any_different = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_different, "Sequential SIMD RNG calls should differ");
}

void test_simd_rng_card_generation(void) {
    simd_rng_state_t rng;
    simd_rng_init(&rng, 12345);

    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* Mark some cards as dead */
    StdDeck_CardMask_SET(dead, 0);
    StdDeck_CardMask_SET(dead, 1);
    StdDeck_CardMask_SET(dead, 2);

    /* Generate 100 cards, all should be valid and not dead */
    for (int i = 0; i < 100; i++) {
        int card = simd_rng_random_card(&rng, 52, &dead);
        TEST_ASSERT_TRUE(card >= 0 && card < 52);
        TEST_ASSERT_FALSE(card == 0 || card == 1 || card == 2);
    }
}

/* ===== Main ===== */

int main(void) {
    srand((unsigned int)time(NULL));

    /* Initialize GPU */
    gpu_eval_config_t config = gpu_eval_default_config();
    config.preferred_backend = GPU_BACKEND_OPENCL;
    config.verbose = 0;
    config.max_batch_size = LARGE_BATCH;

    g_ctx = gpu_eval_init_batched(&config);
    g_gpu_available = (g_ctx != NULL);

    if (!g_gpu_available) {
        printf("No GPU available — GPU tests will be skipped\n");
    }

    UNITY_BEGIN();

    /* Low table tests (no GPU needed) */
    RUN_TEST(test_gpu_low_table_loading);

    /* SIMD RNG tests (no GPU needed) */
    RUN_TEST(test_simd_rng_basic);
    RUN_TEST(test_simd_rng_card_generation);

    /* GPU parity tests */
    RUN_TEST(test_gpu_stud_batch_vs_cpu);
    RUN_TEST(test_gpu_razz_batch_vs_cpu);
    RUN_TEST(test_gpu_omaha_hilo_vs_cpu);
    RUN_TEST(test_gpu_stud_hilo_vs_cpu);
    RUN_TEST(test_gpu_batch_size_scaling);

    int result = UNITY_END();

    if (g_ctx) {
        gpu_eval_free(g_ctx);
    }

    return result;
}
