/**
 * @file test_gpu_opencl_alloc_guards.c
 * @brief Regression test for BUG-13: OpenCL batch hilo eval used int math
 *        for total_hands (n_boards * n_players), overflowing to a negative
 *        count that made malloc return NULL, which was then dereferenced
 *        in the conversion loops. Result buffers were also unchecked.
 *
 * The overflow/zero-input guards run before any ctx field is accessed,
 * so those tests drive a fake context and need no device. The happy-path
 * test requires a real OpenCL device and skips (77) without one.
 */

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/gpu/eval_gpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define SKIP 77

#define CHECK(cond, msg)               \
    do {                               \
        if (!(cond)) {                 \
            fprintf(stderr, "FAIL: %s\n", msg); \
            return 1;                  \
        }                              \
    } while (0)

/* A zeroed fake context: the guard paths return -1 before any field of the
 * context is read, so this must be safe to pass through. backend_type must
 * route the unified dispatcher to the OpenCL backend, which is the one with
 * the overflow bug. */
static gpu_eval_context_t g_fake_ctx = { .backend_type = 1 };

static int test_hilo_overflow_returns_error(void) {
    printf("  test_hilo_overflow_returns_error...");

    /* 46341 * 46341 = 2147488281 > INT_MAX: the old int product wrapped
     * negative, the malloc size became a huge size_t that returned NULL,
     * and the conversion loop dereferenced it (crash). Post-fix the guard
     * must return -1 before touching any input, so NULL pointers are safe
     * to pass here. */
    int n_boards = 46341;
    int n_players = 46341;

    gpu_eval_result_hilo_t result;
    memset(&result, 0, sizeof(result));

    int ret = gpu_eval_batch_boards_hilo(&g_fake_ctx, NULL, NULL, n_boards, n_players, &result);

    /* Overflow must be rejected, not crash. */
    CHECK(ret == -1, "overflowing batch size should return -1");
    CHECK(result.hand_values_hi == NULL, "nothing should be written to result on error");

    printf(" PASSED\n"); fflush(stdout);
    return 0;
}

static int test_zero_boards_returns_error(void) {
    printf("  test_zero_boards_returns_error...");

    gpu_eval_result_hilo_t result;
    memset(&result, 0, sizeof(result));

    int ret = gpu_eval_batch_boards_hilo(&g_fake_ctx, NULL, NULL, 0, 2, &result);

    CHECK(ret == -1, "zero boards should return -1");
    CHECK(result.hand_values_hi == NULL, "nothing should be written to result on error");

    printf(" PASSED\n"); fflush(stdout);
    return 0;
}

static int test_hilo_small_batch_still_works(void) {
    printf("  test_hilo_small_batch_still_works...");

    if (!gpu_is_available(1)) {
        printf(" SKIPPED (no OpenCL device)\n"); fflush(stdout);
        return SKIP;
    }

    gpu_game_config_t config = gpu_game_config_holdem8();
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, 1024, 1, config);
    if (!ctx) {
        printf(" SKIPPED (OpenCL init failed)\n"); fflush(stdout);
        return SKIP;
    }

    int n_boards = 4;
    int n_players = 3;

    StdDeck_CardMask* boards = (StdDeck_CardMask*)malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* holes = (StdDeck_CardMask*)malloc((size_t)n_boards * n_players * sizeof(StdDeck_CardMask));
    CHECK(boards != NULL && holes != NULL, "should allocate inputs");

    /* Valid Hold'em8 inputs: 5-card boards, 2-card holes, no duplicates */
    for (int i = 0; i < n_boards; i++) {
        StdDeck_CardMask_RESET(boards[i]);
        StdDeck_CardMask_SET(boards[i], (i * 3) % 52);
        StdDeck_CardMask_SET(boards[i], (i * 3 + 1) % 52);
        StdDeck_CardMask_SET(boards[i], (i * 3 + 2) % 52);
        StdDeck_CardMask_SET(boards[i], (i * 3 + 14) % 52);
        StdDeck_CardMask_SET(boards[i], (i * 3 + 27) % 52);
    }
    for (size_t i = 0; i < (size_t)n_boards * n_players; i++) {
        StdDeck_CardMask_RESET(holes[i]);
        StdDeck_CardMask_SET(holes[i], (i * 5 + 2) % 52);
        StdDeck_CardMask_SET(holes[i], (i * 5 + 3) % 52);
    }

    gpu_eval_result_hilo_t result;
    memset(&result, 0, sizeof(result));

    int ret = gpu_eval_batch_boards_hilo(ctx, boards, holes, n_boards, n_players, &result);

    free(holes);
    free(boards);
    gpu_eval_cleanup(ctx);

    /* A normal batch must still succeed end-to-end on a real device */
    CHECK(ret == 0, "small valid batch should succeed");

    if (ret == 0) {
        free(result.hand_values_hi);
        free(result.hand_values_lo);
        free(result.lo_qualifies);
    }

    printf(" PASSED\n"); fflush(stdout);
    return 0;
}

int main(void) {
    printf("test_gpu_opencl_alloc_guards\n");
    int failures = 0;
    int skipped = 0;

    int r1 = test_hilo_overflow_returns_error();
    if (r1 == SKIP) skipped++; else failures += r1;

    int r2 = test_zero_boards_returns_error();
    if (r2 == SKIP) skipped++; else failures += r2;

    int r3 = test_hilo_small_batch_still_works();
    if (r3 == SKIP) skipped++; else failures += r3;

    if (skipped == 1) {
        printf("All device-dependent tests skipped (no OpenCL device)\n");
    }
    if (failures > 0) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("All tests passed\n");
    return 0;
}
