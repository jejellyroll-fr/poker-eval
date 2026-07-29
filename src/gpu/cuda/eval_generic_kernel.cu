/*
 * eval_generic_kernel.cu: Generic CUDA kernels for multi-game poker evaluation
 *
 * This file implements generalized GPU kernels that can evaluate multiple
 * poker variants (Hold'em, Omaha, Stud, Razz, hi/lo games, etc.)
 */

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>

#include "eval_cuda_device_common.cuh"

typedef uint32_t LowHandVal;

/* Game configuration (must match host definition) */
typedef enum {
    GPU_GAME_HOLDEM = 0,
    GPU_GAME_OMAHA = 1,
    GPU_GAME_OMAHA5 = 2,
    GPU_GAME_OMAHA6 = 3,
    GPU_GAME_STUD = 4,
    GPU_GAME_RAZZ = 5,
    GPU_GAME_HOLDEM8 = 6,
    GPU_GAME_OMAHA8 = 7,
    GPU_GAME_STUD8 = 8,
    GPU_GAME_LOWBALL27 = 9
} gpu_game_type_t;

typedef struct {
    gpu_game_type_t game;
    int num_hole_cards;
    int num_board_cards;
    int eval_low;
    int split_pot;
    int low_qualifier;
} gpu_game_config_t;

/* Forward declarations for helpers exported by other translation units */
extern "C" {
__device__ LowHandVal cuda_eval_low_a5_device(CudaCardMask cards, int n_cards);
__device__ LowHandVal cuda_eval_low_27_device(CudaCardMask cards, int n_cards);
__device__ int cuda_low_qualifies_device(LowHandVal lo, int qualifier);
__device__ HandVal cuda_eval_omaha_best_hi_device(CudaCardMask hole, CudaCardMask board, int num_hole);
__device__ LowHandVal cuda_eval_omaha_best_lo_device(CudaCardMask hole, CudaCardMask board, int num_hole);
}
__device__ CudaCardMask cuda_combine_masks(CudaCardMask a, CudaCardMask b);
__device__ CudaCardMask cuda_extract_card(CudaCardMask mask, int suit, int rank);
__device__ int cuda_count_cards(CudaCardMask mask);

/*
 * Generic evaluation kernel for all game types
 * This kernel evaluates hands based on game configuration
 */
__global__ void eval_generic_boards_kernel(
    CudaCardMask* boards,
    CudaCardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_game_config_t config,
    HandVal* results_hi,
    HandVal* results_lo,
    int* lo_qualifies
) {
    int board_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (board_idx < n_boards) {
        for (int player = 0; player < n_players; player++) {
            int result_idx = board_idx * n_players + player;
            int hole_idx = board_idx * n_players + player;

            CudaCardMask combined;
            combined.cards_n = config.num_hole_cards + config.num_board_cards;

            /* Combine hole and board cards based on game type */
            if (config.num_board_cards > 0) {
                /* Board games: Hold'em, Omaha */
                CudaCardMask board = boards[board_idx];
                CudaCardMask hole = hole_cards[hole_idx];

                /* Check if we need to do Omaha-style evaluation */
                if (config.game == GPU_GAME_OMAHA ||
                    config.game == GPU_GAME_OMAHA5 ||
                    config.game == GPU_GAME_OMAHA6 ||
                    config.game == GPU_GAME_OMAHA8) {

                    /* Omaha: exactly 2 hole + 3 board */
                    results_hi[result_idx] = cuda_eval_omaha_best_hi_device(
                        hole, board, config.num_hole_cards);

                    if (config.eval_low) {
                        LowHandVal lo = cuda_eval_omaha_best_lo_device(
                            hole, board, config.num_hole_cards);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = cuda_low_qualifies_device(lo, config.low_qualifier);
                    }
                } else {
                    /* Hold'em: use all cards */
                    combined = cuda_combine_masks(board, hole);
                        results_hi[result_idx] = cuda_eval_5cards(combined, 7);

                    if (config.eval_low) {
                        LowHandVal lo = cuda_eval_low_a5_device(combined, 7);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = cuda_low_qualifies_device(lo, config.low_qualifier);
                    }
                }
            } else {
                /* No board games: Stud, Razz */
                combined = hole_cards[hole_idx];

                if (config.game == GPU_GAME_LOWBALL27) {
                    LowHandVal lo = cuda_eval_low_27_device(combined, config.num_hole_cards);
                    results_hi[result_idx] = 0;
                    results_lo[result_idx] = lo;
                    lo_qualifies[result_idx] = cuda_low_qualifies_device(lo, config.low_qualifier);
                } else if (config.eval_low && !config.split_pot) {
                    /* Razz: low only */
                    LowHandVal lo = cuda_eval_low_a5_device(combined, 7);
                    results_lo[result_idx] = lo;
                    results_hi[result_idx] = 0;  /* Not used */
                    lo_qualifies[result_idx] = 1; /* Always qualifies in Razz */
                } else {
                    /* Regular Stud */
                    results_hi[result_idx] = cuda_eval_5cards(combined, 7);

                    if (config.eval_low) {
                        /* Stud Hi/Lo */
                        LowHandVal lo = cuda_eval_low_a5_device(combined, 7);
                        results_lo[result_idx] = lo;
                        lo_qualifies[result_idx] = cuda_low_qualifies_device(lo, config.low_qualifier);
                    }
                }
            }
        }
    }
}

/*
 * Combine two card masks
 */
__device__ CudaCardMask cuda_combine_masks(CudaCardMask a, CudaCardMask b) {
    CudaCardMask result;
    result.cards_n = a.cards_n + b.cards_n;
    result.cards[0] = a.cards[0] | b.cards[0];
    result.cards[1] = a.cards[1] | b.cards[1];
    result.cards[2] = a.cards[2] | b.cards[2];
    result.cards[3] = a.cards[3] | b.cards[3];
    return result;
}

/*
 * Count number of cards in a mask
 */
__device__ int cuda_count_cards(CudaCardMask mask) {
    int count = 0;
    count += __popc(mask.cards[0]); // spades
    count += __popc(mask.cards[1]); // hearts
    count += __popc(mask.cards[2]); // diamonds
    count += __popc(mask.cards[3]); // clubs
    return count;
}

/*
 * Extract a specific card from a mask
 * Returns a mask with only that card set
 */
__device__ CudaCardMask cuda_make_single_card(int suit, int rank) {
    CudaCardMask result;
    result.cards_n = 1;
    result.cards[0] = result.cards[1] = result.cards[2] = result.cards[3] = 0;
    result.cards[suit] = (1U << rank);
    return result;
}

/*
 * Extract cards from a mask at specific indices
 * Used for Omaha combination generation
 */
__device__ CudaCardMask cuda_extract_cards_by_indices(
    CudaCardMask mask,
    int* indices,
    int num_indices
) {
    CudaCardMask result;
    result.cards_n = num_indices;
    result.cards[0] = result.cards[1] = result.cards[2] = result.cards[3] = 0;

    int card_list[13]; // Max cards per mask
    int card_count = 0;

    /* Build list of all cards in mask */
    for (int suit = 0; suit < 4; suit++) {
        uint32_t suit_mask = mask.cards[suit];
        for (int rank = 0; rank < 13; rank++) {
            if (suit_mask & (1U << rank)) {
                if (card_count < 13) {
                    card_list[card_count++] = suit * 13 + rank;
                }
            }
        }
    }

    /* Extract cards at specified indices */
    for (int i = 0; i < num_indices; i++) {
        if (indices[i] < card_count) {
            int card = card_list[indices[i]];
            int suit = card / 13;
            int rank = card % 13;
            result.cards[suit] |= (1U << rank);
        }
    }

    return result;
}

/* Placeholder implementations - to be completed in separate files */

/*
 * Export the kernel launch wrapper
 */
extern "C" int cuda_eval_generic_boards(
    void* d_boards,
    void* d_hole_cards,
    int n_boards,
    int n_players,
    gpu_game_config_t config,
    void* d_results_hi,
    void* d_results_lo,
    void* d_lo_qualifies
) {
    int threads_per_block = 256;
    int num_blocks = (n_boards + threads_per_block - 1) / threads_per_block;

    eval_generic_boards_kernel<<<num_blocks, threads_per_block>>>(
        (CudaCardMask*)d_boards,
        (CudaCardMask*)d_hole_cards,
        n_boards,
        n_players,
        config,
        (HandVal*)d_results_hi,
        (HandVal*)d_results_lo,
        (int*)d_lo_qualifies
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return -1;
    }

    return 0;
}
