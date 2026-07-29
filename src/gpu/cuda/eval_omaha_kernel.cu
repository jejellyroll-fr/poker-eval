/*
 * eval_omaha_kernel.cu: CUDA kernels for Omaha hand evaluation
 *
 * Implements Omaha evaluation: exactly 2 from hole + 3 from board
 * Supports Omaha (4 hole), Omaha-5 (5 hole), and Omaha-6 (6 hole)
 */

#include <cuda_runtime.h>
#include <stdint.h>

#include "eval_cuda_device_common.cuh"

typedef uint32_t LowHandVal;

/* External function declarations */
extern "C" __device__ LowHandVal cuda_eval_low_a5_device(CudaCardMask cards, int n_cards);

/*
 * Extract nth card from a mask
 * Returns suit (0-3) and rank (0-12) via pointers
 */
__device__ int cuda_get_nth_card(CudaCardMask mask, int n, int* suit, int* rank) {
    int count = 0;

    for (int s = 0; s < 4; s++) {
        uint32_t suit_mask = mask.cards[s];
        for (int r = 0; r < 13; r++) {
            if (suit_mask & (1U << r)) {
                if (count == n) {
                    *suit = s;
                    *rank = r;
                    return 1;
                }
                count++;
            }
        }
    }

    return 0; /* Card not found */
}

/*
 * Create a card mask with exactly 5 cards:
 * 2 from hole (at indices h1, h2) and 3 from board (at indices b1, b2, b3)
 */
__device__ CudaCardMask cuda_make_omaha_hand(
    CudaCardMask hole,
    CudaCardMask board,
    int h1, int h2,
    int b1, int b2, int b3
) {
    CudaCardMask result;
    result.cards_n = 5;
    result.cards[0] = result.cards[1] = result.cards[2] = result.cards[3] = 0;

    int suit, rank;

    /* Add 2 hole cards */
    if (cuda_get_nth_card(hole, h1, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (cuda_get_nth_card(hole, h2, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }

    /* Add 3 board cards */
    if (cuda_get_nth_card(board, b1, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (cuda_get_nth_card(board, b2, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }
    if (cuda_get_nth_card(board, b3, &suit, &rank)) {
        result.cards[suit] |= (1U << rank);
    }

    return result;
}

/*
 * Evaluate best high hand for Omaha
 * Tries all C(num_hole, 2) × C(5, 3) = 60 combinations for Omaha-4
 */
__device__ HandVal cuda_eval_omaha_best_hi(
    CudaCardMask hole,
    CudaCardMask board,
    int num_hole
) {
    HandVal best = 0;

    /* Try all combinations of 2 hole cards */
    for (int h1 = 0; h1 < num_hole; h1++) {
        for (int h2 = h1 + 1; h2 < num_hole; h2++) {

            /* Try all combinations of 3 board cards */
            for (int b1 = 0; b1 < 5; b1++) {
                for (int b2 = b1 + 1; b2 < 5; b2++) {
                    for (int b3 = b2 + 1; b3 < 5; b3++) {

                        /* Make 5-card hand */
                        CudaCardMask hand = cuda_make_omaha_hand(
                            hole, board, h1, h2, b1, b2, b3);

                        /* Evaluate it */
                        HandVal val = cuda_eval_5cards(hand, 5);

                        /* Track best */
                        if (val > best) {
                            best = val;
                        }
                    }
                }
            }
        }
    }

    return best;
}

/*
 * Evaluate best low hand for Omaha Hi/Lo
 * Tries all C(num_hole, 2) × C(5, 3) combinations
 * Returns best (lowest) qualifying low hand
 */
__device__ LowHandVal cuda_eval_omaha_best_lo(
    CudaCardMask hole,
    CudaCardMask board,
    int num_hole
) {
    LowHandVal best = 0xFFFFFFFFU; /* Start with worst possible low */

    /* Try all combinations of 2 hole cards */
    for (int h1 = 0; h1 < num_hole; h1++) {
        for (int h2 = h1 + 1; h2 < num_hole; h2++) {

            /* Try all combinations of 3 board cards */
            for (int b1 = 0; b1 < 5; b1++) {
                for (int b2 = b1 + 1; b2 < 5; b2++) {
                    for (int b3 = b2 + 1; b3 < 5; b3++) {

                        /* Make 5-card hand */
                        CudaCardMask hand = cuda_make_omaha_hand(
                            hole, board, h1, h2, b1, b2, b3);

                        /* Evaluate low */
                        LowHandVal val = cuda_eval_low_a5_device(hand, 5);

                        /* Track best low (lower value is better after inversion) */
                        if (val < best) {
                            best = val;
                        }
                    }
                }
            }
        }
    }

    return best;
}

/*
 * Export functions
 */
extern "C" __device__ HandVal cuda_eval_omaha_best_hi_device(
    CudaCardMask hole,
    CudaCardMask board,
    int num_hole
) {
    return cuda_eval_omaha_best_hi(hole, board, num_hole);
}

extern "C" __device__ LowHandVal cuda_eval_omaha_best_lo_device(
    CudaCardMask hole,
    CudaCardMask board,
    int num_hole
) {
    return cuda_eval_omaha_best_lo(hole, board, num_hole);
}
