/*
 * ofc_evaluation.cu - CUDA kernels for OFC hand evaluation
 *
 * High-performance parallel hand evaluation for Open Face Chinese Poker.
 * Implements complete poker hand evaluation optimized for GPU execution.
 *
 * Target: 1000x speedup over CPU baseline for batch evaluations.
 * Designed to work with monte_carlo kernels for complete OFC solution.
 *
 * Copyright (C) 2025
 */

#include <cuda_runtime.h>
#include <stdio.h>

/* OFC evaluation constants */
#define OFC_TOP_CARDS         3
#define OFC_MIDDLE_CARDS      5
#define OFC_BOTTOM_CARDS      5
#define OFC_NUM_HANDS         3

#define STD_DECK_N_CARDS      52
#define CUDA_BLOCK_SIZE       256
#define MAX_HANDS_PER_BATCH   1024

/* Hand rankings for OFC (higher = stronger) */
#define OFC_HIGH_CARD         0
#define OFC_PAIR              1000
#define OFC_TWO_PAIR          2000
#define OFC_THREE_OF_KIND     3000
#define OFC_STRAIGHT          4000
#define OFC_FLUSH             5000
#define OFC_FULL_HOUSE        6000
#define OFC_FOUR_OF_KIND      7000
#define OFC_STRAIGHT_FLUSH    8000
#define OFC_ROYAL_FLUSH       9000

/* Card extraction macros */
#define GPU_CARD_RANK(c)      ((c) >> 2)
#define GPU_CARD_SUIT(c)      ((c) & 3)
#define GPU_MAKE_CARD(r,s)    (((r) << 2) | (s))

/* OFC hand evaluation structure for GPU */
typedef struct {
    unsigned long long card_mask;    /* Bitmask of cards in hand */
    int hand_strength;               /* Calculated hand strength */
    int num_cards;                   /* Number of cards in hand */
} ofc_hand_eval_t;

/* OFC complete hand for evaluation */
typedef struct {
    ofc_hand_eval_t hands[3];        /* Top, middle, bottom */
    int is_valid;                    /* 1 if hierarchy is valid, 0 if foul */
    float total_points;              /* OFC scoring points */
} ofc_complete_hand_t;

/* ===== GPU Device Functions for Hand Evaluation ===== */

/**
 * Count bits in a 64-bit mask (population count)
 */
__device__ int gpu_popcount(unsigned long long mask) {
    return __popcll(mask);
}

/**
 * Find the highest set bit (most significant bit)
 */
__device__ int gpu_clz(unsigned long long mask) {
    return __clzll(mask);
}

/**
 * Extract rank counts from card mask
 */
__device__ void gpu_get_rank_counts(unsigned long long card_mask, int *rank_counts) {
    for (int i = 0; i < 13; i++) {
        rank_counts[i] = 0;
    }

    for (int card = 0; card < 52; card++) {
        if (card_mask & (1ULL << card)) {
            int rank = GPU_CARD_RANK(card);
            rank_counts[rank]++;
        }
    }
}

/**
 * Check for flush (all same suit)
 */
__device__ int gpu_is_flush(unsigned long long card_mask, int num_cards) {
    if (num_cards < 5) return 0;

    int suit_counts[4] = {0, 0, 0, 0};

    for (int card = 0; card < 52; card++) {
        if (card_mask & (1ULL << card)) {
            int suit = GPU_CARD_SUIT(card);
            suit_counts[suit]++;
            if (suit_counts[suit] >= 5) return 1;
        }
    }

    return 0;
}

/**
 * Check for straight
 */
__device__ int gpu_is_straight(unsigned long long card_mask, int num_cards) {
    if (num_cards < 5) return 0;

    int rank_counts[13];
    gpu_get_rank_counts(card_mask, rank_counts);

    /* Check for regular straights */
    int consecutive = 0;
    for (int rank = 12; rank >= 0; rank--) {
        if (rank_counts[rank] > 0) {
            consecutive++;
            if (consecutive >= 5) return 1;
        } else {
            consecutive = 0;
        }
    }

    /* Check for A-2-3-4-5 straight (wheel) */
    if (rank_counts[12] && rank_counts[3] && rank_counts[2] &&
        rank_counts[1] && rank_counts[0]) {
        return 1;
    }

    return 0;
}

/**
 * Comprehensive hand evaluation for OFC
 * Returns hand strength value (higher = better)
 */
__device__ int gpu_evaluate_ofc_hand(unsigned long long card_mask, int num_cards) {
    if (num_cards == 0) return 0;

    int rank_counts[13];
    gpu_get_rank_counts(card_mask, rank_counts);

    /* Count pairs, trips, quads */
    int pairs = 0, trips = 0, quads = 0;
    int pair_rank = -1, trip_rank = -1, quad_rank = -1;

    for (int rank = 12; rank >= 0; rank--) {
        if (rank_counts[rank] == 4) {
            quads++;
            quad_rank = rank;
        } else if (rank_counts[rank] == 3) {
            trips++;
            trip_rank = rank;
        } else if (rank_counts[rank] == 2) {
            pairs++;
            if (pair_rank == -1) pair_rank = rank;
        }
    }

    int is_flush = gpu_is_flush(card_mask, num_cards);
    int is_straight = gpu_is_straight(card_mask, num_cards);

    /* Evaluate hand type */
    if (is_straight && is_flush) {
        /* Check for royal flush */
        if (rank_counts[12] && rank_counts[11] && rank_counts[10] &&
            rank_counts[9] && rank_counts[8]) {
            return OFC_ROYAL_FLUSH + 1000;
        }
        return OFC_STRAIGHT_FLUSH + (trip_rank * 10);
    }

    if (quads > 0) {
        return OFC_FOUR_OF_KIND + (quad_rank * 100);
    }

    if (trips > 0 && pairs > 0) {
        return OFC_FULL_HOUSE + (trip_rank * 100) + (pair_rank * 10);
    }

    if (is_flush) {
        return OFC_FLUSH + (pair_rank * 10);
    }

    if (is_straight) {
        return OFC_STRAIGHT + (trip_rank * 10);
    }

    if (trips > 0) {
        return OFC_THREE_OF_KIND + (trip_rank * 100);
    }

    if (pairs >= 2) {
        return OFC_TWO_PAIR + (pair_rank * 100);
    }

    if (pairs == 1) {
        return OFC_PAIR + (pair_rank * 100);
    }

    /* High card */
    for (int rank = 12; rank >= 0; rank--) {
        if (rank_counts[rank] > 0) {
            return OFC_HIGH_CARD + (rank * 10);
        }
    }

    return 0;
}

/**
 * Calculate OFC scoring points for a complete hand
 */
__device__ float gpu_calculate_ofc_points(ofc_complete_hand_t *complete_hand) {
    if (!complete_hand->is_valid) {
        return -6.0f;  /* Foul penalty */
    }

    float points = 0.0f;

    /* Top hand bonuses (3 cards) */
    int top_strength = complete_hand->hands[0].hand_strength;
    if (top_strength >= OFC_PAIR + (5 * 100)) {  /* 66+ */
        points += 1.0f;
    }
    if (top_strength >= OFC_PAIR + (9 * 100)) {  /* TT+ */
        points += 2.0f;
    }
    if (top_strength >= OFC_PAIR + (12 * 100)) { /* AA */
        points += 4.0f;
    }
    if (top_strength >= OFC_THREE_OF_KIND) {     /* Trips */
        points += 10.0f;
    }

    /* Middle hand bonuses (5 cards) */
    int middle_strength = complete_hand->hands[1].hand_strength;
    if (middle_strength >= OFC_STRAIGHT) {
        points += 2.0f;
    }
    if (middle_strength >= OFC_FLUSH) {
        points += 4.0f;
    }
    if (middle_strength >= OFC_FULL_HOUSE) {
        points += 6.0f;
    }
    if (middle_strength >= OFC_FOUR_OF_KIND) {
        points += 10.0f;
    }
    if (middle_strength >= OFC_STRAIGHT_FLUSH) {
        points += 15.0f;
    }

    /* Bottom hand bonuses (5 cards) */
    int bottom_strength = complete_hand->hands[2].hand_strength;
    if (bottom_strength >= OFC_STRAIGHT) {
        points += 2.0f;
    }
    if (bottom_strength >= OFC_FLUSH) {
        points += 4.0f;
    }
    if (bottom_strength >= OFC_FULL_HOUSE) {
        points += 6.0f;
    }
    if (bottom_strength >= OFC_FOUR_OF_KIND) {
        points += 10.0f;
    }
    if (bottom_strength >= OFC_STRAIGHT_FLUSH) {
        points += 15.0f;
    }
    if (bottom_strength >= OFC_ROYAL_FLUSH) {
        points += 25.0f;
    }

    return points;
}

/* ===== Main Evaluation Kernels ===== */

/**
 * CUDA kernel for batch OFC hand evaluation
 *
 * Grid: [num_hands, 1, 1]
 * Block: [CUDA_BLOCK_SIZE, 1, 1]
 *
 * Each thread evaluates one complete OFC hand
 */
__global__ void ofc_evaluate_hands_kernel(
    __global ofc_complete_hand_t *hands,
    int num_hands
) {
    int hand_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (hand_idx >= num_hands) return;

    ofc_complete_hand_t *hand = &hands[hand_idx];

    /* Evaluate each of the three OFC hands */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        hand->hands[pos].hand_strength = gpu_evaluate_ofc_hand(
            hand->hands[pos].card_mask,
            hand->hands[pos].num_cards
        );
    }

    /* Check hierarchy validity */
    int top_strength = hand->hands[0].hand_strength;
    int middle_strength = hand->hands[1].hand_strength;
    int bottom_strength = hand->hands[2].hand_strength;

    hand->is_valid = (bottom_strength >= middle_strength) &&
                     (middle_strength >= top_strength);

    /* Calculate scoring points */
    hand->total_points = gpu_calculate_ofc_points(hand);
}

/**
 * Optimized kernel for large batches using shared memory
 */
__global__ void ofc_evaluate_hands_optimized_kernel(
    __global ofc_complete_hand_t *hands,
    int num_hands
) {
    extern __shared__ int shared_memory[];

    int hand_idx = blockIdx.x * blockDim.x + threadIdx.x;
    int local_idx = threadIdx.x;

    if (hand_idx >= num_hands) return;

    /* Load hand data to shared memory for faster access */
    __shared__ ofc_complete_hand_t shared_hands[CUDA_BLOCK_SIZE];

    if (local_idx < blockDim.x && hand_idx < num_hands) {
        shared_hands[local_idx] = hands[hand_idx];
    }

    __syncthreads();

    if (hand_idx >= num_hands) return;

    ofc_complete_hand_t *hand = &shared_hands[local_idx];

    /* Evaluate using shared memory data */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        hand->hands[pos].hand_strength = gpu_evaluate_ofc_hand(
            hand->hands[pos].card_mask,
            hand->hands[pos].num_cards
        );
    }

    /* Validate hierarchy */
    hand->is_valid = (hand->hands[2].hand_strength >= hand->hands[1].hand_strength) &&
                     (hand->hands[1].hand_strength >= hand->hands[0].hand_strength);

    /* Calculate points */
    hand->total_points = gpu_calculate_ofc_points(hand);

    __syncthreads();

    /* Write back to global memory */
    if (local_idx < blockDim.x && hand_idx < num_hands) {
        hands[hand_idx] = shared_hands[local_idx];
    }
}

/* ===== Host Interface Functions ===== */

extern "C" {

/**
 * Launch OFC evaluation kernel from CPU
 */
int launch_ofc_evaluate_hands_cuda(
    void *d_hands,
    int num_hands,
    cudaStream_t stream
) {
    if (!d_hands || num_hands <= 0) {
        return -1;
    }

    /* Configure kernel launch parameters */
    int threads_per_block = CUDA_BLOCK_SIZE;
    int blocks_per_grid = (num_hands + threads_per_block - 1) / threads_per_block;

    dim3 grid(blocks_per_grid, 1, 1);
    dim3 block(threads_per_block, 1, 1);

    /* Choose kernel based on batch size */
    if (num_hands >= 1024) {
        /* Use optimized kernel for large batches */
        size_t shared_mem_size = CUDA_BLOCK_SIZE * sizeof(ofc_complete_hand_t);
        ofc_evaluate_hands_optimized_kernel<<<grid, block, shared_mem_size, stream>>>(
            (ofc_complete_hand_t*)d_hands,
            num_hands
        );
    } else {
        /* Use standard kernel */
        ofc_evaluate_hands_kernel<<<grid, block, 0, stream>>>(
            (ofc_complete_hand_t*)d_hands,
            num_hands
        );
    }

    /* Check for kernel launch errors */
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("CUDA evaluation kernel launch error: %s\n", cudaGetErrorString(err));
        return -1;
    }

    return 0;
}

/**
 * Get optimal configuration for evaluation kernel
 */
int get_optimal_evaluation_config(int num_hands, dim3 *grid, dim3 *block) {
    if (!grid || !block) return -1;

    int threads_per_block = min(CUDA_BLOCK_SIZE, num_hands);
    int blocks_per_grid = (num_hands + threads_per_block - 1) / threads_per_block;

    grid->x = blocks_per_grid;
    grid->y = 1;
    grid->z = 1;

    block->x = threads_per_block;
    block->y = 1;
    block->z = 1;

    return 0;
}

}  /* extern "C" */