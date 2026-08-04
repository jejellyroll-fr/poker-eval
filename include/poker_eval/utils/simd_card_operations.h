/*
 * simd_card_operations.h: SIMD-optimized poker hand evaluation
 *
 * This header provides vectorized versions of poker hand evaluation functions
 * using AVX2 and AVX-512 instructions to evaluate multiple hands simultaneously.
 */

#ifndef __SIMD_CARD_OPERATIONS_H__
#define __SIMD_CARD_OPERATIONS_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SIMD capability detection */
typedef enum {
    SIMD_NONE = 0,
    SIMD_SSE2 = 1,
    SIMD_AVX2 = 2,
    SIMD_AVX512 = 3,
    SIMD_NEON = 4
} simd_capability_t;

/* SIMD batch sizes */
#define SIMD_AVX2_BATCH_SIZE    4   /* 4 hands in 256-bit registers */
#define SIMD_AVX512_BATCH_SIZE  8   /* 8 hands in 512-bit registers */
#define SIMD_NEON_BATCH_SIZE    4   /* 4 hands in 128-bit registers */

/* Structure for batched card masks */
typedef struct {
    uint32_t spades[SIMD_AVX512_BATCH_SIZE];
    uint32_t clubs[SIMD_AVX512_BATCH_SIZE];
    uint32_t diamonds[SIMD_AVX512_BATCH_SIZE];
    uint32_t hearts[SIMD_AVX512_BATCH_SIZE];
    int batch_size;
} simd_card_batch_t;

/* Structure for batched results */
typedef struct {
    HandVal results[SIMD_AVX512_BATCH_SIZE];
    int batch_size;
} simd_result_batch_t;

/* SIMD capability detection */
simd_capability_t simd_detect_capability(void);
const char* simd_capability_name(simd_capability_t cap);

/* Batch evaluation functions */
int simd_eval_batch_hands_avx2(const simd_card_batch_t* batch, simd_result_batch_t* results);
int simd_eval_batch_hands_avx512(const simd_card_batch_t* batch, simd_result_batch_t* results);
int simd_eval_batch_hands_neon(const simd_card_batch_t* batch, simd_result_batch_t* results);

/* Adaptive batch evaluation - automatically chooses best SIMD version */
int simd_eval_batch_hands_adaptive(const simd_card_batch_t* batch, simd_result_batch_t* results);

/* Utility functions for batch preparation */
void simd_prepare_batch_from_masks(const StdDeck_CardMask* masks, int count, simd_card_batch_t* batch);
void simd_extract_results_to_array(const simd_result_batch_t* batch, HandVal* results);

/* High-level interface for easy integration */
int simd_eval_multiple_hands(const StdDeck_CardMask* hands, int count, HandVal* results);

/* Benchmark and testing functions */
double simd_benchmark_capability(simd_capability_t cap, int iterations);
int simd_validate_against_scalar(const StdDeck_CardMask* test_hands, int count);

#ifdef __cplusplus
}
#endif

#endif /* __SIMD_CARD_OPERATIONS_H__ */
