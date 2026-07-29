/*
 * eval_cuda_kernel.cu: CUDA kernels for GPU-accelerated poker hand evaluation
 */

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>

#include "eval_cuda_device_common.cuh"

/* Kernel for batch evaluation of multiple boards */
__global__ void eval_batch_boards_kernel(
    CudaCardMask* boards,
    CudaCardMask* hole_cards,
    int n_boards,
    int n_players,
    HandVal* results
) {
    int board_idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (board_idx < n_boards) {
        CudaCardMask board = boards[board_idx];
        
        /* Evaluate each player's hand for this board */
        for (int player = 0; player < n_players; player++) {
            CudaCardMask combined;
            combined.cards_n = 7; /* 5 board + 2 hole cards */
            
            /* Combine board and hole cards */
            int hole_idx = board_idx * n_players + player;
            combined.cards[0] = board.cards[0] | hole_cards[hole_idx].cards[0];
            combined.cards[1] = board.cards[1] | hole_cards[hole_idx].cards[1];
            combined.cards[2] = board.cards[2] | hole_cards[hole_idx].cards[2];
            combined.cards[3] = board.cards[3] | hole_cards[hole_idx].cards[3];
            
            /* Evaluate the 7-card hand */
            HandVal val = cuda_eval_5cards(combined, 7);
            results[board_idx * n_players + player] = val;
        }
    }
}

/* Simple XORShift RNG for GPU */
__device__ uint32_t xorshift32(uint32_t state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/* Generate random card from available deck */
__device__ int random_card(uint32_t* rng_state, uint64_t used_cards) {
    int card;
    uint64_t card_mask;
    int attempts = 0;
    
    do {
        *rng_state = xorshift32(*rng_state);
        card = (*rng_state) % 52;
        card_mask = 1ULL << card;
        attempts++;
    } while ((used_cards & card_mask) && attempts < 100);
    
    return (attempts < 100) ? card : -1;
}

/* Convert card index to CardMask */
__device__ CudaCardMask card_to_mask(int card) {
    CudaCardMask mask;
    mask.cards_n = 1;
    mask.cards[0] = mask.cards[1] = mask.cards[2] = mask.cards[3] = 0;
    
    int suit = card / 13;
    int rank = card % 13;
    uint32_t rank_mask = 1U << rank;
    
    mask.cards[suit] = rank_mask;
    return mask;
}

/* Combine two card masks */
__device__ CudaCardMask combine_masks(CudaCardMask a, CudaCardMask b) {
    CudaCardMask result;
    result.cards_n = a.cards_n + b.cards_n;
    result.cards[0] = a.cards[0] | b.cards[0];
    result.cards[1] = a.cards[1] | b.cards[1];
    result.cards[2] = a.cards[2] | b.cards[2];
    result.cards[3] = a.cards[3] | b.cards[3];
    return result;
}

/* Kernel for Monte Carlo equity calculation */
__global__ void monte_carlo_equity_kernel(
    CudaCardMask* ranges,
    int n_players,
    int n_simulations,
    int* wins,
    int* ties,
    uint32_t* rng_states
) {
    int sim_idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (sim_idx < n_simulations) {
        /* Initialize thread-local RNG */
        uint32_t rng_state = rng_states[sim_idx];
        
        /* Track used cards */
        uint64_t used_cards = 0;
        
        /* Deal hole cards for each player */
        CudaCardMask player_hands[10]; /* Max 10 players */
        for (int p = 0; p < n_players && p < 10; p++) {
            player_hands[p].cards_n = 0;
            player_hands[p].cards[0] = player_hands[p].cards[1] = 
            player_hands[p].cards[2] = player_hands[p].cards[3] = 0;
            
            /* Deal 2 hole cards per player */
            for (int c = 0; c < 2; c++) {
                int card = random_card(&rng_state, used_cards);
                if (card >= 0) {
                    used_cards |= (1ULL << card);
                    CudaCardMask card_mask = card_to_mask(card);
                    player_hands[p] = combine_masks(player_hands[p], card_mask);
                }
            }
        }
        
        /* Deal 5 board cards */
        CudaCardMask board;
        board.cards_n = 0;
        board.cards[0] = board.cards[1] = board.cards[2] = board.cards[3] = 0;
        
        for (int c = 0; c < 5; c++) {
            int card = random_card(&rng_state, used_cards);
            if (card >= 0) {
                used_cards |= (1ULL << card);
                CudaCardMask card_mask = card_to_mask(card);
                board = combine_masks(board, card_mask);
            }
        }
        
        /* Evaluate all hands */
        HandVal hand_values[10];
        for (int p = 0; p < n_players && p < 10; p++) {
            CudaCardMask combined = combine_masks(player_hands[p], board);
            hand_values[p] = cuda_eval_5cards(combined, 7);
        }
        
        /* Find winner(s) */
        HandVal best_hand = 0;
        int winner_count = 0;
        
        for (int p = 0; p < n_players; p++) {
            if (hand_values[p] > best_hand) {
                best_hand = hand_values[p];
                winner_count = 1;
            } else if (hand_values[p] == best_hand) {
                winner_count++;
            }
        }
        
        /* Update win/tie counts */
        for (int p = 0; p < n_players; p++) {
            if (hand_values[p] == best_hand) {
                if (winner_count == 1) {
                    atomicAdd(&wins[p], 1);
                } else {
                    atomicAdd(&ties[p], 1);
                }
            }
        }
        
        /* Update RNG state for next iteration */
        rng_states[sim_idx] = rng_state;
    }
}

/* Host-side functions to copy lookup tables to GPU constant memory */
extern "C" void cuda_init_lookup_tables(
    const uint8_t* nBitsTable,
    const uint8_t* straightTable,
    const uint32_t* topFiveCardsTable,
    const uint8_t* topCardTable
) {
    cudaMemcpyToSymbol(d_nBitsTable, nBitsTable, 8192 * sizeof(uint8_t));
    cudaMemcpyToSymbol(d_straightTable, straightTable, 8192 * sizeof(uint8_t));
    cudaMemcpyToSymbol(d_topFiveCardsTable, topFiveCardsTable, 8192 * sizeof(uint32_t));
    cudaMemcpyToSymbol(d_topCardTable, topCardTable, 8192 * sizeof(uint8_t));
}

/* Host-side wrapper for batch evaluation */
extern "C" int cuda_eval_batch_boards(
    void* d_boards,
    void* d_hole_cards,
    int n_boards,
    int n_players,
    void* d_results
) {
    /* Configure kernel launch parameters */
    int threads_per_block = 256;
    int blocks = (n_boards + threads_per_block - 1) / threads_per_block;
    
    /* Launch kernel */
    eval_batch_boards_kernel<<<blocks, threads_per_block>>>(
        (CudaCardMask*)d_boards,
        (CudaCardMask*)d_hole_cards,
        n_boards,
        n_players,
        (HandVal*)d_results
    );
    
    /* Check for errors */
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return -1;
    }
    
    /* Wait for kernel completion */
    cudaDeviceSynchronize();
    
    return 0;
}

/* Host-side wrapper for Monte Carlo simulation */
extern "C" int cuda_monte_carlo_equity(
    void* d_ranges,
    int n_players,
    int n_simulations,
    void* d_wins,
    void* d_ties,
    void* d_rng_states
) {
    /* Configure kernel launch parameters */
    int threads_per_block = 256;
    int blocks = (n_simulations + threads_per_block - 1) / threads_per_block;
    
    /* Launch kernel */
    monte_carlo_equity_kernel<<<blocks, threads_per_block>>>(
        (CudaCardMask*)d_ranges,
        n_players,
        n_simulations,
        (int*)d_wins,
        (int*)d_ties,
        (uint32_t*)d_rng_states
    );
    
    /* Check for errors */
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return -1;
    }
    
    /* Wait for kernel completion */
    cudaDeviceSynchronize();
    
    return 0;
}
