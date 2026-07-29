/*
 * ofc_evaluation.cl - OpenCL kernels for OFC hand evaluation
 *
 * OpenCL version of CUDA hand evaluation kernels for broader GPU compatibility.
 * Implements complete poker hand evaluation optimized for parallel execution.
 *
 * Target: 1000x speedup over CPU baseline for batch evaluations.
 * Compatible with: AMD GPUs, Intel GPUs, NVIDIA GPUs via OpenCL
 *
 * Copyright (C) 2025
 */

/* OFC evaluation constants */
#define OFC_TOP_CARDS         3
#define OFC_MIDDLE_CARDS      5
#define OFC_BOTTOM_CARDS      5
#define OFC_NUM_HANDS         3

#define STD_DECK_N_CARDS      52
#define OPENCL_BLOCK_SIZE     256
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

/* OFC hand evaluation structure for OpenCL */
typedef struct {
    ulong card_mask;          /* Bitmask of cards in hand */
    int hand_strength;        /* Calculated hand strength */
    int num_cards;           /* Number of cards in hand */
} ofc_hand_eval_t;

/* OFC complete hand for evaluation */
typedef struct {
    ofc_hand_eval_t hands[3]; /* Top, middle, bottom */
    int is_valid;             /* 1 if hierarchy is valid, 0 if foul */
    float total_points;       /* OFC scoring points */
} ofc_complete_hand_t;

/* ===== OpenCL Device Functions for Hand Evaluation ===== */

/**
 * Count bits in a 64-bit mask (population count)
 */
int gpu_popcount_ocl(ulong mask) {
    return popcount(mask);
}

/**
 * Extract rank counts from card mask
 */
void gpu_get_rank_counts_ocl(ulong card_mask, __private int *rank_counts) {
    for (int i = 0; i < 13; i++) {
        rank_counts[i] = 0;
    }

    for (int card = 0; card < 52; card++) {
        if (card_mask & (1UL << card)) {
            int rank = GPU_CARD_RANK(card);
            rank_counts[rank]++;
        }
    }
}

/**
 * Check for flush (all same suit)
 */
int gpu_is_flush_ocl(ulong card_mask, int num_cards) {
    if (num_cards < 5) return 0;

    int suit_counts[4] = {0, 0, 0, 0};

    for (int card = 0; card < 52; card++) {
        if (card_mask & (1UL << card)) {
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
int gpu_is_straight_ocl(ulong card_mask, int num_cards) {
    if (num_cards < 5) return 0;

    int rank_counts[13];
    gpu_get_rank_counts_ocl(card_mask, rank_counts);

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
int gpu_evaluate_ofc_hand_ocl(ulong card_mask, int num_cards) {
    if (num_cards == 0) return 0;

    int rank_counts[13];
    gpu_get_rank_counts_ocl(card_mask, rank_counts);

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

    int is_flush = gpu_is_flush_ocl(card_mask, num_cards);
    int is_straight = gpu_is_straight_ocl(card_mask, num_cards);

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
float gpu_calculate_ofc_points_ocl(ofc_complete_hand_t *complete_hand) {
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
 * OpenCL kernel for batch OFC hand evaluation
 *
 * Global work size: [num_hands]
 * Local work size: [OPENCL_BLOCK_SIZE]
 *
 * Each work-item evaluates one complete OFC hand
 */
__kernel void ofc_evaluate_hands_opencl(
    __global ofc_complete_hand_t *hands,
    int num_hands
) {
    int hand_idx = get_global_id(0);

    if (hand_idx >= num_hands) return;

    ofc_complete_hand_t *hand = &hands[hand_idx];

    /* Evaluate each of the three OFC hands */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        hand->hands[pos].hand_strength = gpu_evaluate_ofc_hand_ocl(
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
    hand->total_points = gpu_calculate_ofc_points_ocl(hand);
}

/**
 * Optimized kernel using local memory for faster access
 */
__kernel void ofc_evaluate_hands_opencl_optimized(
    __global ofc_complete_hand_t *hands,
    int num_hands
) {
    int global_id = get_global_id(0);
    int local_id = get_local_id(0);
    int group_id = get_group_id(0);
    int local_size = get_local_size(0);

    /* Local memory for batch processing */
    __local ofc_complete_hand_t local_hands[OPENCL_BLOCK_SIZE];

    if (global_id >= num_hands) return;

    /* Load data to local memory */
    if (local_id < local_size && global_id < num_hands) {
        local_hands[local_id] = hands[global_id];
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (global_id >= num_hands) return;

    ofc_complete_hand_t *hand = &local_hands[local_id];

    /* Evaluate using local memory data */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        hand->hands[pos].hand_strength = gpu_evaluate_ofc_hand_ocl(
            hand->hands[pos].card_mask,
            hand->hands[pos].num_cards
        );
    }

    /* Validate hierarchy */
    hand->is_valid = (hand->hands[2].hand_strength >= hand->hands[1].hand_strength) &&
                     (hand->hands[1].hand_strength >= hand->hands[0].hand_strength);

    /* Calculate points */
    hand->total_points = gpu_calculate_ofc_points_ocl(hand);

    barrier(CLK_LOCAL_MEM_FENCE);

    /* Write back to global memory */
    if (local_id < local_size && global_id < num_hands) {
        hands[global_id] = local_hands[local_id];
    }
}

/**
 * Specialized kernel for small batches with detailed evaluation
 */
__kernel void ofc_evaluate_hands_opencl_detailed(
    __global ofc_complete_hand_t *hands,
    __global int *detailed_strengths,    /* Detailed strength breakdown */
    int num_hands
) {
    int hand_idx = get_global_id(0);

    if (hand_idx >= num_hands) return;

    ofc_complete_hand_t *hand = &hands[hand_idx];

    /* Evaluate with detailed breakdown */
    for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
        int strength = gpu_evaluate_ofc_hand_ocl(
            hand->hands[pos].card_mask,
            hand->hands[pos].num_cards
        );

        hand->hands[pos].hand_strength = strength;

        /* Store detailed strength in output array */
        detailed_strengths[hand_idx * OFC_NUM_HANDS + pos] = strength;
    }

    /* Validate hierarchy */
    hand->is_valid = (hand->hands[2].hand_strength >= hand->hands[1].hand_strength) &&
                     (hand->hands[1].hand_strength >= hand->hands[0].hand_strength);

    /* Calculate points */
    hand->total_points = gpu_calculate_ofc_points_ocl(hand);
}