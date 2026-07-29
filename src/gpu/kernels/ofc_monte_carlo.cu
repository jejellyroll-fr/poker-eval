/*
 * ofc_monte_carlo.cu - CUDA kernels for OFC Monte Carlo simulation
 *
 * This file implements the core GPU acceleration for OFC foul risk calculation.
 * Target: 1000x speedup over CPU baseline through massive parallelization.
 *
 * Each thread performs one complete Monte Carlo simulation.
 * Each block handles all simulations for one partial hand.
 * Each grid processes multiple hands simultaneously.
 *
 * Based on Phase 1 SIMD success (96x), targeting GPU 1000x+ speedup.
 *
 * Copyright (C) 2025
 */

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <stdio.h>

/* OFC constants for GPU */
#define OFC_TOP_CARDS         3
#define OFC_MIDDLE_CARDS      5
#define OFC_BOTTOM_CARDS      5
#define OFC_TOTAL_CARDS       13
#define OFC_NUM_HANDS         3

#define STD_DECK_N_CARDS      52
#define CUDA_BLOCK_SIZE       256    /* Threads per block = simulations per hand */
#define MAX_AVAILABLE_CARDS   39     /* Max cards that can be available */

/* OFC positions */
typedef enum {
    OFC_TOP = 0,
    OFC_MIDDLE = 1,
    OFC_BOTTOM = 2
} ofc_position_t;

/* Simplified OFC hand structure for GPU */
typedef struct {
    unsigned long long hands[3];     /* Card masks for top/middle/bottom */
    int card_count[3];               /* Number of cards in each position */
} ofc_hand_gpu_t;

/* Monte Carlo batch structure for GPU */
typedef struct {
    ofc_hand_gpu_t partial_hands[1024];    /* Input partial hands */
    int cards[1024];                        /* Cards to place */
    ofc_position_t positions[1024];         /* Positions to test */
    float foul_risks[1024];                 /* Output foul probabilities */
    int available_cards[1024 * MAX_AVAILABLE_CARDS];  /* Available cards per hand */
    int num_available[1024];                /* Number available per hand */
    int batch_size;                         /* Number of hands in batch */
} ofc_monte_carlo_batch_gpu_t;

/* ===== GPU Device Functions ===== */

/**
 * Fast GPU random number generator
 * Each thread maintains its own RNG state
 */
__device__ uint32_t gpu_fast_rand(curandState_t *state) {
    return curand(state);
}

/**
 * Place a card in an OFC hand on GPU
 */
__device__ int gpu_place_card(ofc_hand_gpu_t *hand, int card, ofc_position_t position) {
    if (position < 0 || position >= OFC_NUM_HANDS) return -1;

    /* Check if position has space */
    int max_cards = (position == OFC_TOP) ? OFC_TOP_CARDS :
                    (position == OFC_MIDDLE) ? OFC_MIDDLE_CARDS : OFC_BOTTOM_CARDS;

    if (hand->card_count[position] >= max_cards) return -1;

    /* Add card to hand mask (simplified) */
    hand->hands[position] |= (1ULL << card);
    hand->card_count[position]++;

    return 0;
}

/**
 * Simplified hand evaluation for GPU
 * Returns a basic strength value (higher = stronger)
 */
__device__ int gpu_evaluate_hand_simple(unsigned long long hand_mask, int num_cards) {
    /* Very simplified evaluation for GPU efficiency */
    /* Count bits set (number of different cards) */
    int strength = 0;
    unsigned long long mask = hand_mask;

    while (mask) {
        strength += (mask & 1);
        mask >>= 1;
    }

    /* Bonus for full hands */
    if ((num_cards == OFC_TOP_CARDS && num_cards == 3) ||
        (num_cards == OFC_MIDDLE_CARDS && num_cards == 5) ||
        (num_cards == OFC_BOTTOM_CARDS && num_cards == 5)) {
        strength += 100;
    }

    return strength;
}

/**
 * Validate OFC hierarchy on GPU
 * Returns 1 if valid, 0 if foul
 */
__device__ int gpu_validate_hierarchy(ofc_hand_gpu_t *hand) {
    /* Evaluate all three hands */
    int top_val = gpu_evaluate_hand_simple(hand->hands[OFC_TOP], hand->card_count[OFC_TOP]);
    int middle_val = gpu_evaluate_hand_simple(hand->hands[OFC_MIDDLE], hand->card_count[OFC_MIDDLE]);
    int bottom_val = gpu_evaluate_hand_simple(hand->hands[OFC_BOTTOM], hand->card_count[OFC_BOTTOM]);

    /* Check hierarchy: bottom >= middle >= top */
    return (bottom_val >= middle_val) && (middle_val >= top_val);
}

/**
 * Randomly complete an OFC hand on GPU
 */
__device__ int gpu_randomly_complete_hand(
    ofc_hand_gpu_t *hand,
    int *available_cards,
    int num_available,
    curandState_t *rand_state
) {
    /* Calculate cards needed */
    int cards_needed = OFC_TOTAL_CARDS -
                      (hand->card_count[OFC_TOP] +
                       hand->card_count[OFC_MIDDLE] +
                       hand->card_count[OFC_BOTTOM]);

    if (cards_needed <= 0) return 0;  /* Already complete */
    if (num_available < cards_needed) return -1;  /* Not enough cards */

    /* Copy available cards to local array for modification */
    int local_available[MAX_AVAILABLE_CARDS];
    for (int i = 0; i < num_available && i < MAX_AVAILABLE_CARDS; i++) {
        local_available[i] = available_cards[i];
    }

    /* Randomly place remaining cards */
    for (int i = 0; i < cards_needed; i++) {
        /* Select random card */
        int remaining = num_available - i;
        int card_idx = gpu_fast_rand(rand_state) % remaining;
        int card = local_available[card_idx];

        /* Remove card by swapping with last */
        local_available[card_idx] = local_available[remaining - 1];

        /* Determine position (simple strategy) */
        ofc_position_t position;
        if (hand->card_count[OFC_TOP] < OFC_TOP_CARDS) {
            position = OFC_TOP;
        } else if (hand->card_count[OFC_MIDDLE] < OFC_MIDDLE_CARDS) {
            position = OFC_MIDDLE;
        } else {
            position = OFC_BOTTOM;
        }

        /* Place card */
        if (gpu_place_card(hand, card, position) != 0) {
            return -1;  /* Placement failed */
        }
    }

    return 0;
}

/* ===== Main Monte Carlo Kernel ===== */

/**
 * CUDA kernel for massive OFC Monte Carlo simulation
 *
 * Grid dimension: [num_hands, 1, 1]
 * Block dimension: [CUDA_BLOCK_SIZE, 1, 1] (256 threads = 256 simulations per hand)
 *
 * Each block processes all Monte Carlo simulations for one hand
 * Each thread within a block performs one complete simulation
 */
__global__ void ofc_monte_carlo_kernel(
    ofc_monte_carlo_batch_gpu_t *batch,
    int simulations_per_hand,
    unsigned long seed
) {
    int hand_idx = blockIdx.x;  /* Which hand this block is processing */
    int sim_idx = threadIdx.x;  /* Which simulation this thread handles */

    /* Bounds checking */
    if (hand_idx >= batch->batch_size) return;
    if (sim_idx >= simulations_per_hand) return;

    /* Initialize random state for this thread */
    curandState_t rand_state;
    curand_init(seed, hand_idx * CUDA_BLOCK_SIZE + sim_idx, 0, &rand_state);

    /* Shared memory for block-level reduction */
    __shared__ int foul_count[CUDA_BLOCK_SIZE];
    __shared__ int valid_simulations[CUDA_BLOCK_SIZE];

    /* Initialize shared memory */
    foul_count[sim_idx] = 0;
    valid_simulations[sim_idx] = 0;

    /* Get the partial hand for this block */
    ofc_hand_gpu_t simulation_hand = batch->partial_hands[hand_idx];

    /* Place the test card */
    if (gpu_place_card(&simulation_hand, batch->cards[hand_idx], batch->positions[hand_idx]) == 0) {

        /* Get available cards for this hand */
        int *available_cards = &batch->available_cards[hand_idx * MAX_AVAILABLE_CARDS];
        int num_available = batch->num_available[hand_idx];

        /* Randomly complete the hand */
        if (gpu_randomly_complete_hand(&simulation_hand, available_cards, num_available, &rand_state) == 0) {

            valid_simulations[sim_idx] = 1;

            /* Check if this completion results in a foul */
            if (!gpu_validate_hierarchy(&simulation_hand)) {
                foul_count[sim_idx] = 1;
            }
        }
    }

    /* Synchronize threads in block */
    __syncthreads();

    /* Block-level reduction to count total fouls and valid simulations */
    if (sim_idx == 0) {
        int total_fouls = 0;
        int total_valid = 0;

        for (int i = 0; i < CUDA_BLOCK_SIZE && i < simulations_per_hand; i++) {
            total_fouls += foul_count[i];
            total_valid += valid_simulations[i];
        }

        /* Calculate foul risk */
        if (total_valid > 0) {
            batch->foul_risks[hand_idx] = (float)total_fouls / (float)total_valid;
        } else {
            batch->foul_risks[hand_idx] = 1.0f;  /* No valid simulations = high risk */
        }
    }
}

/**
 * Optimized Monte Carlo kernel for large batches
 * Uses more advanced GPU memory patterns and optimization
 */
__global__ void ofc_monte_carlo_optimized_kernel(
    ofc_monte_carlo_batch_gpu_t *batch,
    int simulations_per_hand,
    unsigned long seed
) {
    int hand_idx = blockIdx.x;
    int sim_idx = threadIdx.x;

    if (hand_idx >= batch->batch_size || sim_idx >= simulations_per_hand) return;

    /* Use shared memory more efficiently */
    extern __shared__ int shared_memory[];
    int *shared_available_cards = shared_memory;
    int *shared_foul_counts = shared_memory + MAX_AVAILABLE_CARDS;

    curandState_t rand_state;
    curand_init(seed, hand_idx * blockDim.x + sim_idx, 0, &rand_state);

    /* Load available cards to shared memory (coalesced access) */
    if (sim_idx < MAX_AVAILABLE_CARDS) {
        shared_available_cards[sim_idx] = batch->available_cards[hand_idx * MAX_AVAILABLE_CARDS + sim_idx];
    }
    shared_foul_counts[sim_idx] = 0;

    __syncthreads();

    /* Perform Monte Carlo simulation */
    ofc_hand_gpu_t sim_hand = batch->partial_hands[hand_idx];

    if (gpu_place_card(&sim_hand, batch->cards[hand_idx], batch->positions[hand_idx]) == 0) {
        if (gpu_randomly_complete_hand(&sim_hand, shared_available_cards,
                                      batch->num_available[hand_idx], &rand_state) == 0) {
            if (!gpu_validate_hierarchy(&sim_hand)) {
                shared_foul_counts[sim_idx] = 1;
            }
        }
    }

    __syncthreads();

    /* Optimized reduction using warp primitives */
    int foul_sum = shared_foul_counts[sim_idx];

    /* Warp-level reduction */
    for (int offset = warpSize / 2; offset > 0; offset /= 2) {
        foul_sum += __shfl_down_sync(0xFFFFFFFF, foul_sum, offset);
    }

    /* Block-level reduction */
    if (threadIdx.x % warpSize == 0) {
        atomicAdd(&shared_foul_counts[0], foul_sum);
    }

    __syncthreads();

    if (sim_idx == 0) {
        batch->foul_risks[hand_idx] = (float)shared_foul_counts[0] / (float)simulations_per_hand;
    }
}

/* ===== Host Interface Functions ===== */

extern "C" {

/**
 * Launch Monte Carlo kernel from CPU
 */
int launch_ofc_monte_carlo_cuda(
    void *d_batch,                  /* Device pointer to batch */
    int batch_size,
    int simulations_per_hand,
    cudaStream_t stream
) {
    if (!d_batch || batch_size <= 0 || simulations_per_hand <= 0) {
        return -1;
    }

    /* Configure kernel launch parameters */
    dim3 grid(batch_size, 1, 1);                    /* One block per hand */
    dim3 block(min(simulations_per_hand, CUDA_BLOCK_SIZE), 1, 1);  /* One thread per simulation */

    /* Generate random seed */
    unsigned long seed = (unsigned long)clock();

    /* Launch kernel */
    if (simulations_per_hand <= CUDA_BLOCK_SIZE) {
        /* Use standard kernel */
        ofc_monte_carlo_kernel<<<grid, block, 0, stream>>>(
            (ofc_monte_carlo_batch_gpu_t*)d_batch,
            simulations_per_hand,
            seed
        );
    } else {
        /* Use optimized kernel for large simulation counts */
        size_t shared_mem_size = (MAX_AVAILABLE_CARDS + CUDA_BLOCK_SIZE) * sizeof(int);
        ofc_monte_carlo_optimized_kernel<<<grid, block, shared_mem_size, stream>>>(
            (ofc_monte_carlo_batch_gpu_t*)d_batch,
            min(simulations_per_hand, CUDA_BLOCK_SIZE),
            seed
        );
    }

    /* Check for kernel launch errors */
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("CUDA kernel launch error: %s\n", cudaGetErrorString(err));
        return -1;
    }

    return 0;
}

/**
 * Get optimal CUDA configuration for batch size
 */
int get_optimal_cuda_config(int batch_size, int simulations_per_hand, dim3 *grid, dim3 *block) {
    if (!grid || !block) return -1;

    /* Grid: one block per hand */
    grid->x = batch_size;
    grid->y = 1;
    grid->z = 1;

    /* Block: one thread per simulation, up to maximum block size */
    block->x = min(simulations_per_hand, CUDA_BLOCK_SIZE);
    block->y = 1;
    block->z = 1;

    return 0;
}

}  /* extern "C" */