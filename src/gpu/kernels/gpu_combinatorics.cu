/*
 * gpu_combinatorics.cu - GPU Combinatorics with Parallel Scans/Prefix-sum
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

#include <stdint.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

/* ===== Combinatorial Utilities ===== */

/*
 * Compute binomial coefficient C(n,k) = n! / (k! * (n-k)!)
 * Uses Pascal's triangle for efficiency
 */
__device__ uint64_t binomial_coeff(int n, int k) {
    if (k > n) return 0;
    if (k == 0 || k == n) return 1;
    if (k > n - k) k = n - k;  /* Optimization: C(n,k) = C(n,n-k) */

    uint64_t result = 1;
    for (int i = 0; i < k; i++) {
        result = result * (n - i) / (i + 1);
    }

    return result;
}

/*
 * Convert combination index to actual combination (unranking)
 * Uses combinatorial number system
 *
 * Example: C(5,3) has 10 combinations
 * index=0 → {0,1,2}, index=1 → {0,1,3}, ..., index=9 → {2,3,4}
 */
__device__ void unrank_combination(
    uint64_t index,
    int n,
    int k,
    int* out_combination  /* Output: k elements */
) {
    int c = 0;
    int r = k;

    for (int i = 0; i < k; i++) {
        /* Find largest m such that C(m, r) <= index */
        while (binomial_coeff(c, r) <= index) {
            c++;
        }
        c--;

        out_combination[i] = n - 1 - c;
        index -= binomial_coeff(c, r);
        r--;
    }

    /* Sort combination in ascending order */
    for (int i = 0; i < k; i++) {
        out_combination[i] = n - 1 - out_combination[i];
    }
}

/*
 * Convert combination to index (ranking)
 * Inverse of unrank_combination
 */
__device__ uint64_t rank_combination(
    const int* combination,
    int n,
    int k
) {
    uint64_t rank = 0;

    for (int i = 0; i < k; i++) {
        if (i == 0) {
            for (int j = 0; j < combination[i]; j++) {
                rank += binomial_coeff(n - 1 - j, k - 1);
            }
        } else {
            for (int j = combination[i-1] + 1; j < combination[i]; j++) {
                rank += binomial_coeff(n - 1 - j, k - i - 1);
            }
        }
    }

    return rank;
}

/* ===== Parallel Prefix Sum (Scan) ===== */

/*
 * Work-efficient parallel scan (Blelloch algorithm)
 * Computes exclusive prefix sum in O(n) work and O(log n) depth
 *
 * Input:  [3, 1, 7, 0, 4, 1, 6, 3]
 * Output: [0, 3, 4, 11, 11, 15, 16, 22]  (exclusive scan)
 */
__global__ void prefix_sum_exclusive_kernel(
    const uint32_t* input,
    uint32_t* output,
    uint32_t n
) {
    extern __shared__ uint32_t temp[];

    int tid = threadIdx.x;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    /* Load input into shared memory */
    temp[2*tid] = (2*idx < n) ? input[2*idx] : 0;
    temp[2*tid + 1] = (2*idx + 1 < n) ? input[2*idx + 1] : 0;

    int offset = 1;

    /* Up-sweep (reduce) phase */
    for (int d = blockDim.x; d > 0; d >>= 1) {
        __syncthreads();

        if (tid < d) {
            int ai = offset * (2*tid + 1) - 1;
            int bi = offset * (2*tid + 2) - 1;

            temp[bi] += temp[ai];
        }

        offset *= 2;
    }

    /* Clear the last element */
    if (tid == 0) {
        temp[2*blockDim.x - 1] = 0;
    }

    /* Down-sweep phase */
    for (int d = 1; d < 2*blockDim.x; d *= 2) {
        offset >>= 1;
        __syncthreads();

        if (tid < d) {
            int ai = offset * (2*tid + 1) - 1;
            int bi = offset * (2*tid + 2) - 1;

            uint32_t t = temp[ai];
            temp[ai] = temp[bi];
            temp[bi] += t;
        }
    }

    __syncthreads();

    /* Write results to device memory */
    if (2*idx < n) output[2*idx] = temp[2*tid];
    if (2*idx + 1 < n) output[2*idx + 1] = temp[2*tid + 1];
}

/* ===== Combination Generation ===== */

/*
 * Generate all C(n,k) combinations in parallel
 * Each thread generates one combination
 */
__global__ void generate_all_combinations_kernel(
    int n,
    int k,
    int* out_combinations,  /* Output: [num_combinations * k] */
    uint64_t num_combinations
) {
    uint64_t idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= num_combinations) return;

    /* Unrank to get combination */
    int combination[32];  /* Max k=32 */

    if (k <= 32) {
        unrank_combination(idx, n, k, combination);

        /* Write to global memory */
        for (int i = 0; i < k; i++) {
            out_combinations[idx * k + i] = combination[i];
        }
    }
}

/*
 * Generate C(52,5) poker hands (all 2,598,960 hands)
 * Specialized for poker hand generation
 */
__global__ void generate_all_poker_hands_kernel(
    uint8_t* out_hands,  /* Output: [2598960 * 5] card indices */
    uint64_t num_hands
) {
    uint64_t idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= num_hands) return;

    int hand[5];
    unrank_combination(idx, 52, 5, hand);

    /* Write to global memory */
    for (int i = 0; i < 5; i++) {
        out_hands[idx * 5 + i] = (uint8_t)hand[i];
    }
}

/*
 * Generate C(7,5) board combinations for equity calculation
 * Each thread generates 21 combinations from its 7-card hand
 */
__global__ void generate_board_combinations_kernel(
    const uint8_t* seven_card_hands,  /* Input: [batch_size * 7] */
    uint8_t* five_card_boards,        /* Output: [batch_size * 21 * 5] */
    uint64_t batch_size
) {
    uint64_t hand_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (hand_idx >= batch_size) return;

    const uint8_t* hand = &seven_card_hands[hand_idx * 7];

    /* Generate all 21 combinations C(7,5) */
    for (int comb_idx = 0; comb_idx < 21; comb_idx++) {
        int indices[5];
        unrank_combination(comb_idx, 7, 5, indices);

        /* Map to actual cards */
        uint8_t* board = &five_card_boards[(hand_idx * 21 + comb_idx) * 5];
        for (int i = 0; i < 5; i++) {
            board[i] = hand[indices[i]];
        }
    }
}

/* ===== Compact/Stream Compaction ===== */

/*
 * Stream compaction: remove elements where predicate is false
 * Uses prefix sum to compute output indices
 *
 * Example:
 * Input:     [3, 1, 7, 0, 4, 1, 6, 3]
 * Predicate: [1, 0, 1, 0, 1, 0, 1, 1]  (keep if non-zero)
 * Output:    [3, 7, 4, 6, 3]
 */
__global__ void stream_compact_kernel(
    const uint32_t* input,
    const uint8_t* predicate,  /* 1 = keep, 0 = remove */
    const uint32_t* scan,      /* Exclusive prefix sum of predicate */
    uint32_t* output,
    uint32_t n,
    uint32_t* out_size
) {
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= n) return;

    if (predicate[idx]) {
        uint32_t out_idx = scan[idx];
        output[out_idx] = input[idx];

        /* Last thread writes total count */
        if (idx == n - 1) {
            *out_size = scan[idx] + 1;
        }
    } else if (idx == n - 1 && scan[idx] == 0) {
        /* No elements kept */
        *out_size = 0;
    }
}

/* ===== Host API ===== */

extern "C" {

/*
 * Launch prefix sum kernel
 */
int gpu_prefix_sum_exclusive(
    const uint32_t* d_input,
    uint32_t* d_output,
    uint32_t n,
    cudaStream_t stream
) {
    if (n == 0) return 0;

    int threads = 256;
    int blocks = (n + 2*threads - 1) / (2*threads);

    size_t shared_mem = 2 * threads * sizeof(uint32_t);

    prefix_sum_exclusive_kernel<<<blocks, threads, shared_mem, stream>>>(
        d_input, d_output, n
    );

    return cudaGetLastError();
}

/*
 * Generate all C(n,k) combinations
 */
int gpu_generate_all_combinations(
    int n,
    int k,
    int* d_out_combinations,
    cudaStream_t stream
) {
    if (k > 32) return -1;  /* Max k supported */

    uint64_t num_comb = 1;
    for (int i = 0; i < k; i++) {
        num_comb = num_comb * (n - i) / (i + 1);
    }

    int threads = 256;
    int blocks = (num_comb + threads - 1) / threads;

    generate_all_combinations_kernel<<<blocks, threads, 0, stream>>>(
        n, k, d_out_combinations, num_comb
    );

    return cudaGetLastError();
}

/*
 * Generate all C(52,5) poker hands
 */
int gpu_generate_all_poker_hands(
    uint8_t* d_out_hands,
    cudaStream_t stream
) {
    const uint64_t num_hands = 2598960;  /* C(52,5) */

    int threads = 256;
    int blocks = (num_hands + threads - 1) / threads;

    generate_all_poker_hands_kernel<<<blocks, threads, 0, stream>>>(
        d_out_hands, num_hands
    );

    return cudaGetLastError();
}

/*
 * Generate C(7,5) board combinations for batch of 7-card hands
 */
int gpu_generate_board_combinations(
    const uint8_t* d_seven_card_hands,
    uint8_t* d_five_card_boards,
    uint64_t batch_size,
    cudaStream_t stream
) {
    int threads = 256;
    int blocks = (batch_size + threads - 1) / threads;

    generate_board_combinations_kernel<<<blocks, threads, 0, stream>>>(
        d_seven_card_hands, d_five_card_boards, batch_size
    );

    return cudaGetLastError();
}

} /* extern "C" */
