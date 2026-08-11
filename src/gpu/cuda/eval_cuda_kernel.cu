/*
 * eval_cuda_kernel.cu: CUDA kernels for GPU-accelerated poker hand evaluation
 */

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>

#include "eval_cuda_device_common.cuh"

/* ===== Advanced RNG (xorshift128+) =====
 *
 * Replaces the original 32-bit XORShift32 generator. Each simulation carries a
 * 128-bit xorshift128+ state (two uint64), which has a far longer period
 * (2^128 - 1) and much better statistical quality than XORShift32, and crucially
 * a per-simulation, per-run seed via splitmix64 so independent simulations stay
 * decorrelated. No curand dependency, so it compiles under the strict -Werror
 * project flags without the CUDA system-header friction. */

/* splitmix64: turns a 64-bit seed into a well-scrambled 64-bit value. */
__device__ __host__ static inline uint64_t splitmix64(uint64_t* seed) {
    uint64_t z = (*seed += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* xorshift128+: advances the 128-bit state and returns a 64-bit value. */
__device__ static inline uint64_t xorshift128p_next(uint64_t s[2]) {
    uint64_t s1 = s[0];
    const uint64_t s0 = s[1];
    s[0] = s0;
    s1 ^= s1 << 23;
    s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
    return s[1] + s0;
}

/* Uniform integer in [0, n). */
__device__ static inline unsigned int rng_uniform(uint64_t s[2], unsigned int n) {
    return (unsigned int)(xorshift128p_next(s) % (uint64_t)n);
}

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

/* Generate a random card from the still-available deck using the xorshift128+
 * generator. Returns -1 when the deck is exhausted (caller should treat this
 * as an error rather than silently producing a short hand). */
__device__ int random_card(uint64_t rng_state[2], uint64_t used_cards) {
    int avail[52];
    int n = 0;
    for (int card = 0; card < 52; card++) {
        if (!(used_cards & (1ULL << card))) {
            avail[n++] = card;
        }
    }
    if (n == 0) {
        return -1;
    }
    int idx = (int)rng_uniform(rng_state, (unsigned int)n);
    return avail[idx];
}

/* Generate a random card that is (a) still available and (b) present in the
 * given per-player range mask. Card indices follow the StdDeck layout
 * (suit = card / 13, rank = card % 13) matching the card_to_mask mapping. */
__device__ int random_card_from_range(uint64_t rng_state[2], uint64_t used_cards,
                                      CudaCardMask range) {
    /* Collect still-available cards that the player's range allows. */
    int avail[52];
    int n = 0;
    for (int suit = 0; suit < 4; suit++) {
        uint32_t suit_ranks = range.cards[suit];
        for (int rank = 0; rank < 13; rank++) {
            if (suit_ranks & (1u << rank)) {
                int card = suit * 13 + rank;
                if (!(used_cards & (1ULL << card)) && n < 52) {
                    avail[n++] = card;
                }
            }
        }
    }
    if (n == 0) {
        return -1;
    }
    int idx = (int)rng_uniform(rng_state, (unsigned int)n);
    return avail[idx];
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

/* Kernel that seeds the per-simulation xorshift128+ states. A dedicated seed
 * pass keeps the Monte Carlo kernel free of splitmix64's setup cost on every
 * iteration and lets the caller reuse the state buffer across runs. Each
 * simulation gets a distinct, well-scrambled 128-bit seed derived from the
 * base seed and its index. */
__global__ void monte_carlo_init_rng_kernel(
    uint64_t* rng_states,
    unsigned long long seed,
    int n_simulations
) {
    int sim_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (sim_idx < n_simulations) {
        uint64_t s = seed + (uint64_t)sim_idx * 0x9E3779B97F4A7C15ULL;
        rng_states[sim_idx * 2 + 0] = splitmix64(&s);
        rng_states[sim_idx * 2 + 1] = splitmix64(&s);
    }
}

/* Kernel for Monte Carlo equity calculation */
__global__ void monte_carlo_equity_kernel(
    CudaCardMask* ranges,
    int n_players,
    int n_simulations,
    int* wins,
    int* ties,
    uint64_t* rng_states
) {
    int sim_idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (sim_idx < n_simulations) {
        /* Initialize thread-local RNG state from the seeded buffer. */
        uint64_t rng_state[2] = { rng_states[sim_idx * 2 + 0],
                                  rng_states[sim_idx * 2 + 1] };
        
        /* Track used cards */
        uint64_t used_cards = 0;
        
        /* Deal hole cards for each player */
        CudaCardMask player_hands[10]; /* Max 10 players */
        for (int p = 0; p < n_players && p < 10; p++) {
            player_hands[p].cards_n = 0;
            player_hands[p].cards[0] = player_hands[p].cards[1] = 
            player_hands[p].cards[2] = player_hands[p].cards[3] = 0;
            
            /* Deal 2 hole cards per player. When a per-player range mask is
               provided, hole cards are drawn only from that range. */
            for (int c = 0; c < 2; c++) {
                int card;
                if (ranges != NULL) {
                    card = random_card_from_range(rng_state, used_cards, ranges[p]);
                } else {
                    card = random_card(rng_state, used_cards);
                }
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
            int card = random_card(rng_state, used_cards);
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
        rng_states[sim_idx * 2 + 0] = rng_state[0];
        rng_states[sim_idx * 2 + 1] = rng_state[1];
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

    /* Seed the xorshift128+ generators once before the equity pass. The caller
     * owns the state buffer (two uint64 per simulation) but is expected to pass
     * freshly allocated device memory, so advance the base seed by a fixed
     * offset to decorrelate successive runs. */
    static unsigned long long rng_seed = 0x9E3779B97F4A7C15ULL;
    monte_carlo_init_rng_kernel<<<blocks, threads_per_block>>>(
        (uint64_t*)d_rng_states,
        rng_seed,
        n_simulations
    );
    rng_seed = rng_seed * 6364136223846793005ULL + 1442695040888963407ULL;

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return -1;
    }
    if (cudaDeviceSynchronize() != cudaSuccess) {
        return -1;
    }

    /* Launch kernel */
    monte_carlo_equity_kernel<<<blocks, threads_per_block>>>(
        (CudaCardMask*)d_ranges,
        n_players,
        n_simulations,
        (int*)d_wins,
        (int*)d_ties,
        (uint64_t*)d_rng_states
    );

    /* Check for errors */
    err = cudaGetLastError();
    if (err != cudaSuccess) {
        return -1;
    }

    /* Wait for kernel completion */
    cudaDeviceSynchronize();

    return 0;
}
