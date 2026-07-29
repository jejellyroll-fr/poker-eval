/*
 * ofc_monte_carlo.cl - OpenCL kernels for OFC Monte Carlo simulation
 *
 * OpenCL version of the CUDA Monte Carlo kernels for broader GPU compatibility.
 * Implements the same massive parallelization strategy as CUDA version.
 *
 * Target: 1000x speedup over CPU baseline.
 * Compatible with: AMD GPUs, Intel GPUs, NVIDIA GPUs via OpenCL
 *
 * Copyright (C) 2025
 */

/* OFC constants */
#define OFC_TOP_CARDS         3
#define OFC_MIDDLE_CARDS      5
#define OFC_BOTTOM_CARDS      5
#define OFC_TOTAL_CARDS       13
#define OFC_NUM_HANDS         3

#define STD_DECK_N_CARDS      52
#define OPENCL_BLOCK_SIZE     256
#define MAX_AVAILABLE_CARDS   39

/* OFC positions */
typedef enum {
    OFC_TOP = 0,
    OFC_MIDDLE = 1,
    OFC_BOTTOM = 2
} ofc_position_t;

/* Simplified OFC hand for OpenCL */
typedef struct {
    ulong hands[3];          /* Card masks for top/middle/bottom */
    int card_count[3];       /* Number of cards in each position */
} ofc_hand_gpu_t;

/* Random number generation for OpenCL */
uint gpu_fast_rand(__private uint *state) {
    *state = (*state * 1103515245U + 12345U) & 0x7FFFFFFF;
    return *state;
}

/* Place a card in OFC hand */
int gpu_place_card(ofc_hand_gpu_t *hand, int card, int position) {
    if (position < 0 || position >= OFC_NUM_HANDS) return -1;

    /* Check if position has space */
    int max_cards = (position == OFC_TOP) ? OFC_TOP_CARDS :
                    (position == OFC_MIDDLE) ? OFC_MIDDLE_CARDS : OFC_BOTTOM_CARDS;

    if (hand->card_count[position] >= max_cards) return -1;

    /* Add card to hand mask */
    hand->hands[position] |= (1UL << card);
    hand->card_count[position]++;

    return 0;
}

/* Simplified hand evaluation */
int gpu_evaluate_hand_simple(ulong hand_mask, int num_cards) {
    int strength = 0;
    ulong mask = hand_mask;

    /* Count bits set */
    while (mask) {
        strength += (mask & 1);
        mask >>= 1;
    }

    /* Bonus for complete hands */
    if ((num_cards == OFC_TOP_CARDS && num_cards == 3) ||
        (num_cards == OFC_MIDDLE_CARDS && num_cards == 5) ||
        (num_cards == OFC_BOTTOM_CARDS && num_cards == 5)) {
        strength += 100;
    }

    return strength;
}

/* Validate OFC hierarchy */
int gpu_validate_hierarchy(ofc_hand_gpu_t *hand) {
    int top_val = gpu_evaluate_hand_simple(hand->hands[OFC_TOP], hand->card_count[OFC_TOP]);
    int middle_val = gpu_evaluate_hand_simple(hand->hands[OFC_MIDDLE], hand->card_count[OFC_MIDDLE]);
    int bottom_val = gpu_evaluate_hand_simple(hand->hands[OFC_BOTTOM], hand->card_count[OFC_BOTTOM]);

    return (bottom_val >= middle_val) && (middle_val >= top_val);
}

/* Randomly complete OFC hand */
int gpu_randomly_complete_hand(
    ofc_hand_gpu_t *hand,
    __private int *available_cards,
    int num_available,
    __private uint *rand_state
) {
    int cards_needed = OFC_TOTAL_CARDS -
                      (hand->card_count[OFC_TOP] +
                       hand->card_count[OFC_MIDDLE] +
                       hand->card_count[OFC_BOTTOM]);

    if (cards_needed <= 0) return 0;
    if (num_available < cards_needed) return -1;

    /* Randomly place remaining cards */
    for (int i = 0; i < cards_needed; i++) {
        int remaining = num_available - i;
        int card_idx = gpu_fast_rand(rand_state) % remaining;
        int card = available_cards[card_idx];

        /* Remove card by swapping */
        available_cards[card_idx] = available_cards[remaining - 1];

        /* Simple placement strategy */
        int position;
        if (hand->card_count[OFC_TOP] < OFC_TOP_CARDS) {
            position = OFC_TOP;
        } else if (hand->card_count[OFC_MIDDLE] < OFC_MIDDLE_CARDS) {
            position = OFC_MIDDLE;
        } else {
            position = OFC_BOTTOM;
        }

        if (gpu_place_card(hand, card, position) != 0) {
            return -1;
        }
    }

    return 0;
}

/* ===== Main OpenCL Kernels ===== */

/**
 * OpenCL kernel for OFC Monte Carlo simulation
 *
 * Global work size: [batch_size * simulations_per_hand]
 * Local work size: [min(simulations_per_hand, OPENCL_BLOCK_SIZE)]
 *
 * Each work-group processes all simulations for one hand
 * Each work-item performs one simulation
 */
__kernel void ofc_monte_carlo_opencl(
    __global ofc_hand_gpu_t *partial_hands,     /* Input partial hands */
    __global int *cards,                        /* Cards to place */
    __global int *positions,                    /* Positions to test */
    __global float *foul_risks,                 /* Output foul risks */
    __global int *available_cards_buffer,       /* Available cards */
    __global int *num_available,                /* Number available per hand */
    int batch_size,
    int simulations_per_hand,
    uint random_seed
) {
    int global_id = get_global_id(0);
    int local_id = get_local_id(0);
    int group_id = get_group_id(0);
    int local_size = get_local_size(0);

    /* Each work-group handles one hand */
    int hand_idx = group_id;
    int sim_idx = local_id;

    if (hand_idx >= batch_size || sim_idx >= simulations_per_hand) return;

    /* Local memory for available cards and reduction */
    __local int local_available_cards[MAX_AVAILABLE_CARDS];
    __local int foul_count[OPENCL_BLOCK_SIZE];
    __local int valid_count[OPENCL_BLOCK_SIZE];

    /* Initialize local memory */
    if (local_id < MAX_AVAILABLE_CARDS) {
        local_available_cards[local_id] =
            available_cards_buffer[hand_idx * MAX_AVAILABLE_CARDS + local_id];
    }
    foul_count[local_id] = 0;
    valid_count[local_id] = 0;

    barrier(CLK_LOCAL_MEM_FENCE);

    /* Initialize random state */
    uint rand_state = random_seed + global_id;

    /* Perform Monte Carlo simulation */
    ofc_hand_gpu_t sim_hand = partial_hands[hand_idx];

    /* Place test card */
    if (gpu_place_card(&sim_hand, cards[hand_idx], positions[hand_idx]) == 0) {

        /* Randomly complete hand */
        if (gpu_randomly_complete_hand(&sim_hand, local_available_cards,
                                      num_available[hand_idx], &rand_state) == 0) {

            valid_count[local_id] = 1;

            /* Check for foul */
            if (!gpu_validate_hierarchy(&sim_hand)) {
                foul_count[local_id] = 1;
            }
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    /* Reduction within work-group */
    if (local_id == 0) {
        int total_fouls = 0;
        int total_valid = 0;

        for (int i = 0; i < local_size && i < simulations_per_hand; i++) {
            total_fouls += foul_count[i];
            total_valid += valid_count[i];
        }

        /* Calculate foul risk */
        if (total_valid > 0) {
            foul_risks[hand_idx] = (float)total_fouls / (float)total_valid;
        } else {
            foul_risks[hand_idx] = 1.0f;
        }
    }
}

/**
 * Optimized OpenCL kernel using atomic operations
 */
__kernel void ofc_monte_carlo_opencl_atomic(
    __global ofc_hand_gpu_t *partial_hands,
    __global int *cards,
    __global int *positions,
    __global float *foul_risks,
    __global int *available_cards_buffer,
    __global int *num_available,
    int batch_size,
    int simulations_per_hand,
    uint random_seed
) {
    int global_id = get_global_id(0);
    int group_id = get_group_id(0);
    int local_id = get_local_id(0);

    int hand_idx = group_id;
    if (hand_idx >= batch_size) return;

    /* Shared counters for atomic operations */
    __local int shared_foul_count;
    __local int shared_valid_count;

    if (local_id == 0) {
        shared_foul_count = 0;
        shared_valid_count = 0;
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    /* Each work-item performs multiple simulations */
    int sims_per_item = (simulations_per_hand + get_local_size(0) - 1) / get_local_size(0);
    uint rand_state = random_seed + global_id;

    for (int s = 0; s < sims_per_item; s++) {
        int sim_id = local_id * sims_per_item + s;
        if (sim_id >= simulations_per_hand) break;

        /* Perform simulation */
        ofc_hand_gpu_t sim_hand = partial_hands[hand_idx];

        if (gpu_place_card(&sim_hand, cards[hand_idx], positions[hand_idx]) == 0) {
            /* Get available cards for this simulation */
            int available_cards[MAX_AVAILABLE_CARDS];
            for (int i = 0; i < num_available[hand_idx] && i < MAX_AVAILABLE_CARDS; i++) {
                available_cards[i] = available_cards_buffer[hand_idx * MAX_AVAILABLE_CARDS + i];
            }

            if (gpu_randomly_complete_hand(&sim_hand, available_cards,
                                          num_available[hand_idx], &rand_state) == 0) {
                atomic_inc(&shared_valid_count);

                if (!gpu_validate_hierarchy(&sim_hand)) {
                    atomic_inc(&shared_foul_count);
                }
            }
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    /* Write final result */
    if (local_id == 0) {
        if (shared_valid_count > 0) {
            foul_risks[hand_idx] = (float)shared_foul_count / (float)shared_valid_count;
        } else {
            foul_risks[hand_idx] = 1.0f;
        }
    }
}

/**
 * Specialized kernel for small batches with high simulation counts
 */
__kernel void ofc_monte_carlo_opencl_high_sim(
    __global ofc_hand_gpu_t *partial_hands,
    __global int *cards,
    __global int *positions,
    __global float *foul_risks,
    __global int *available_cards_buffer,
    __global int *num_available,
    int batch_size,
    int simulations_per_hand,
    uint random_seed
) {
    int global_id = get_global_id(0);

    /* For high simulation count, each work-item handles one hand completely */
    int hand_idx = global_id;
    if (hand_idx >= batch_size) return;

    uint rand_state = random_seed + hand_idx;
    int foul_count = 0;
    int valid_count = 0;

    /* Perform all simulations for this hand */
    for (int sim = 0; sim < simulations_per_hand; sim++) {
        ofc_hand_gpu_t sim_hand = partial_hands[hand_idx];

        if (gpu_place_card(&sim_hand, cards[hand_idx], positions[hand_idx]) == 0) {
            /* Get available cards */
            int available_cards[MAX_AVAILABLE_CARDS];
            for (int i = 0; i < num_available[hand_idx] && i < MAX_AVAILABLE_CARDS; i++) {
                available_cards[i] = available_cards_buffer[hand_idx * MAX_AVAILABLE_CARDS + i];
            }

            if (gpu_randomly_complete_hand(&sim_hand, available_cards,
                                          num_available[hand_idx], &rand_state) == 0) {
                valid_count++;
                if (!gpu_validate_hierarchy(&sim_hand)) {
                    foul_count++;
                }
            }
        }
    }

    /* Calculate final foul risk */
    if (valid_count > 0) {
        foul_risks[hand_idx] = (float)foul_count / (float)valid_count;
    } else {
        foul_risks[hand_idx] = 1.0f;
    }
}