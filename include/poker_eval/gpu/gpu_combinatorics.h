/*
 * gpu_combinatorics.h - GPU Combinatorics with Parallel Scans/Prefix-sum
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Efficient parallel generation of combinations using:
 * - Parallel prefix-sum (scan) for indexing
 * - Combinatorial number system for ranking/unranking
 * - Batch generation of all C(n,k) combinations
 *
 * Use cases:
 * - Generate all C(52,5) poker hands on GPU
 * - Enumerate C(7,5) board combinations for equity
 * - Range enumeration for massive parallel equity calculation
 */

#ifndef GPU_COMBINATORICS_H
#define GPU_COMBINATORICS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== CUDA Stream Type ===== */

#ifdef ENABLE_CUDA
#include <cuda_runtime.h>
typedef cudaStream_t gpu_stream_t;
#else
typedef void* gpu_stream_t;
#endif

/* ===== Parallel Prefix Sum (Scan) ===== */

/**
 * Compute exclusive prefix sum (scan) in parallel
 *
 * Implements work-efficient Blelloch algorithm:
 * - Work: O(n)
 * - Depth: O(log n)
 *
 * Example:
 *   Input:  [3, 1, 7, 0, 4, 1, 6, 3]
 *   Output: [0, 3, 4, 11, 11, 15, 16, 22]
 *
 * @param d_input  Input array (device memory)
 * @param d_output Output array (device memory)
 * @param n        Number of elements
 * @param stream   CUDA stream (optional, can be 0)
 * @return 0 on success, negative on error
 */
int gpu_prefix_sum_exclusive(
    const uint32_t* d_input,
    uint32_t* d_output,
    uint32_t n,
    gpu_stream_t stream
);

/* ===== Combination Generation ===== */

/**
 * Generate all C(n,k) combinations in parallel
 *
 * Each thread generates one combination using unranking algorithm.
 * Combinations are stored in row-major format.
 *
 * Example: C(5,3) = 10 combinations
 *   [0,1,2], [0,1,3], [0,1,4], [0,2,3], [0,2,4],
 *   [0,3,4], [1,2,3], [1,2,4], [1,3,4], [2,3,4]
 *
 * @param n                   Set size (e.g., 52 for standard deck)
 * @param k                   Combination size (max 32)
 * @param d_out_combinations  Output array [C(n,k) * k] (device memory)
 * @param stream              CUDA stream (optional)
 * @return 0 on success, -1 if k > 32, negative on error
 */
int gpu_generate_all_combinations(
    int n,
    int k,
    int* d_out_combinations,
    gpu_stream_t stream
);

/**
 * Generate all C(52,5) poker hands in parallel
 *
 * Specialized kernel for generating all 2,598,960 five-card poker hands.
 * Each hand is represented as 5 card indices (0-51).
 *
 * Memory requirement: 2,598,960 * 5 = 12,994,800 bytes (~13 MB)
 *
 * @param d_out_hands  Output array [2598960 * 5] of card indices (device memory)
 * @param stream       CUDA stream (optional)
 * @return 0 on success, negative on error
 */
int gpu_generate_all_poker_hands(
    uint8_t* d_out_hands,
    gpu_stream_t stream
);

/**
 * Generate C(7,5) board combinations for batch of 7-card hands
 *
 * For each 7-card hand, generates all 21 possible 5-card boards.
 * Used for Hold'em/Stud hand evaluation.
 *
 * Example: 7-card hand [Ah,Kh,Qh,Jh,Th,9h,8h]
 *   → 21 boards: [Ah,Kh,Qh,Jh,Th], [Ah,Kh,Qh,Jh,9h], ...
 *
 * @param d_seven_card_hands  Input array [batch_size * 7] (device memory)
 * @param d_five_card_boards  Output array [batch_size * 21 * 5] (device memory)
 * @param batch_size          Number of 7-card hands
 * @param stream              CUDA stream (optional)
 * @return 0 on success, negative on error
 */
int gpu_generate_board_combinations(
    const uint8_t* d_seven_card_hands,
    uint8_t* d_five_card_boards,
    uint64_t batch_size,
    gpu_stream_t stream
);

/* ===== Stream Compaction ===== */

/**
 * Stream compaction: filter elements where predicate is true
 *
 * Uses parallel scan to compute output indices.
 *
 * Example:
 *   Input:     [3, 1, 7, 0, 4, 1, 6, 3]
 *   Predicate: [1, 0, 1, 0, 1, 0, 1, 1]  (1 = keep, 0 = remove)
 *   Output:    [3, 7, 4, 6, 3]
 *
 * @param d_input     Input array (device memory)
 * @param d_predicate Predicate array (1 = keep, 0 = remove) (device memory)
 * @param d_scan      Exclusive prefix sum of predicate (device memory)
 * @param d_output    Output array (device memory, size >= n)
 * @param n           Number of elements
 * @param d_out_size  Output size (device memory, single uint32_t)
 * @param stream      CUDA stream (optional)
 * @return 0 on success, negative on error
 */
int gpu_stream_compact(
    const uint32_t* d_input,
    const uint8_t* d_predicate,
    const uint32_t* d_scan,
    uint32_t* d_output,
    uint32_t n,
    uint32_t* d_out_size,
    gpu_stream_t stream
);

/* ===== Helper Functions ===== */

/**
 * Compute binomial coefficient C(n,k) on host
 *
 * @param n Set size
 * @param k Subset size
 * @return C(n,k)
 */
uint64_t gpu_binomial_coeff_host(int n, int k);

/**
 * Unrank combination: convert index to actual combination
 *
 * @param index         Combination index (0 to C(n,k)-1)
 * @param n             Set size
 * @param k             Subset size
 * @param out_combination Output array [k] (host memory)
 */
void gpu_unrank_combination_host(
    uint64_t index,
    int n,
    int k,
    int* out_combination
);

/**
 * Rank combination: convert combination to index
 *
 * @param combination Combination array [k] (host memory)
 * @param n           Set size
 * @param k           Subset size
 * @return Index (0 to C(n,k)-1)
 */
uint64_t gpu_rank_combination_host(
    const int* combination,
    int n,
    int k
);

#ifdef __cplusplus
}
#endif

#endif /* GPU_COMBINATORICS_H */
