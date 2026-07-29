/*
 * eval_cuda.c: CUDA implementation of GPU-accelerated poker evaluator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cuda_runtime.h>
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/core/poker_defs.h>

// Function prototypes to avoid -Werror=missing-prototypes

gpu_eval_context_t* cuda_gpu_eval_init(int device_id, size_t max_batch_size, int backend_type);
void cuda_gpu_eval_cleanup(gpu_eval_context_t* ctx);
int cuda_gpu_eval_batch_boards(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
);
int cuda_gpu_monte_carlo_equity(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities
);
int cuda_gpu_get_device_info(int device_id, char* name, size_t* memory, int* compute_units);
int cuda_gpu_is_available(int backend_type);

/* External CUDA kernel functions */
extern void cuda_init_lookup_tables(
    const uint8_t* nBitsTable,
    const uint8_t* straightTable,
    const uint32_t* topFiveCardsTable,
    const uint8_t* topCardTable
);

extern int cuda_eval_batch_boards(
    void* d_boards,
    void* d_hole_cards,
    int n_boards,
    int n_players,
    void* d_results
);

extern int cuda_monte_carlo_equity(
    void* d_ranges,
    int n_players,
    int n_simulations,
    void* d_wins,
    void* d_ties,
    void* d_rng_states
);

/* External lookup tables from the main library */

/* CUDA-compatible card mask structure */
typedef struct {
    uint32_t cards_n;
    uint32_t cards[4]; /* spades, hearts, diamonds, clubs */
} CudaCardMask;

/* Convert StdDeck_CardMask to CudaCardMask */
static CudaCardMask convert_to_cuda_cardmask(StdDeck_CardMask std_mask) {
    CudaCardMask cuda_mask;
    cuda_mask.cards_n = 0; /* Will be set based on how many cards are set */
    cuda_mask.cards[0] = StdDeck_CardMask_SPADES(std_mask);    /* spades */
    cuda_mask.cards[1] = StdDeck_CardMask_HEARTS(std_mask);    /* hearts */
    cuda_mask.cards[2] = StdDeck_CardMask_DIAMONDS(std_mask);  /* diamonds */
    cuda_mask.cards[3] = StdDeck_CardMask_CLUBS(std_mask);     /* clubs */
    return cuda_mask;
}

/* CUDA-specific context structure */
typedef struct {
    gpu_eval_context_t base;
    cudaStream_t stream;
    void* d_boards;
    void* d_hole_cards;
    void* d_results;
    void* d_wins;
    void* d_ties;
    void* d_rng_states;
    CudaCardMask* h_boards_converted;      /* Host buffer for converted boards */
    CudaCardMask* h_hole_cards_converted;  /* Host buffer for converted hole cards */
    size_t boards_capacity;
    size_t hole_cards_capacity;
    size_t results_capacity;
    size_t monte_carlo_capacity;
} cuda_eval_context_t;

/* Initialize CUDA context */
gpu_eval_context_t* cuda_gpu_eval_init(int device_id, size_t max_batch_size, int backend_type) {
    if (backend_type != 0) { /* Only handle CUDA in this implementation */
        return NULL;
    }
    
    cuda_eval_context_t* ctx = (cuda_eval_context_t*)calloc(1, sizeof(cuda_eval_context_t));
    if (!ctx) {
        return NULL;
    }

    /* Set CUDA device - use device 0 if -1 (auto-select) */
    int actual_device_id = (device_id < 0) ? 0 : device_id;
    cudaError_t err = cudaSetDevice(actual_device_id);
    if (err != cudaSuccess) {
        free(ctx);
        return NULL;
    }

    /* Initialize base context */
    ctx->base.device_id = actual_device_id;
    ctx->base.max_batch_size = max_batch_size;
    ctx->base.backend_type = 0; /* CUDA */
    
    /* Create CUDA stream */
    err = cudaStreamCreate(&ctx->stream);
    if (err != cudaSuccess) {
        free(ctx);
        return NULL;
    }
    
    /* Copy lookup tables to GPU constant memory */
    cuda_init_lookup_tables(nBitsTable, straightTable, topFiveCardsTable, topCardTable);
    
    /* Pre-allocate device memory for maximum batch size */
    size_t board_size = max_batch_size * sizeof(CudaCardMask);
    size_t hole_cards_size = max_batch_size * 10 * sizeof(CudaCardMask); /* Up to 10 players */
    size_t results_size = max_batch_size * 10 * sizeof(HandVal);
    size_t monte_carlo_size = max_batch_size * 1000; /* For Monte Carlo simulations */
    
    err = cudaMalloc(&ctx->d_boards, board_size);
    if (err != cudaSuccess) {
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    err = cudaMalloc(&ctx->d_hole_cards, hole_cards_size);
    if (err != cudaSuccess) {
        cudaFree(ctx->d_boards);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    err = cudaMalloc(&ctx->d_results, results_size);
    if (err != cudaSuccess) {
        cudaFree(ctx->d_boards);
        cudaFree(ctx->d_hole_cards);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    /* Allocate memory for Monte Carlo simulation */
    err = cudaMalloc(&ctx->d_wins, 10 * sizeof(int)); /* Up to 10 players */
    if (err != cudaSuccess) {
        cudaFree(ctx->d_boards);
        cudaFree(ctx->d_hole_cards);
        cudaFree(ctx->d_results);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    err = cudaMalloc(&ctx->d_ties, 10 * sizeof(int));
    if (err != cudaSuccess) {
        cudaFree(ctx->d_boards);
        cudaFree(ctx->d_hole_cards);
        cudaFree(ctx->d_results);
        cudaFree(ctx->d_wins);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    err = cudaMalloc(&ctx->d_rng_states, monte_carlo_size * sizeof(uint32_t));
    if (err != cudaSuccess) {
        cudaFree(ctx->d_boards);
        cudaFree(ctx->d_hole_cards);
        cudaFree(ctx->d_results);
        cudaFree(ctx->d_wins);
        cudaFree(ctx->d_ties);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }
    
    ctx->boards_capacity = max_batch_size;
    ctx->hole_cards_capacity = max_batch_size * 10;
    ctx->results_capacity = max_batch_size * 10;
    ctx->monte_carlo_capacity = monte_carlo_size;

    /* Allocate host buffers for conversion */
    ctx->h_boards_converted = (CudaCardMask*)malloc(max_batch_size * sizeof(CudaCardMask));
    ctx->h_hole_cards_converted = (CudaCardMask*)malloc(max_batch_size * 10 * sizeof(CudaCardMask));

    if (!ctx->h_boards_converted || !ctx->h_hole_cards_converted) {
        free(ctx->h_boards_converted);
        free(ctx->h_hole_cards_converted);
        cudaFree(ctx->d_boards);
        cudaFree(ctx->d_hole_cards);
        cudaFree(ctx->d_results);
        cudaFree(ctx->d_wins);
        cudaFree(ctx->d_ties);
        cudaFree(ctx->d_rng_states);
        cudaStreamDestroy(ctx->stream);
        free(ctx);
        return NULL;
    }

    return (gpu_eval_context_t*)ctx;
}

/* Cleanup CUDA context */
void cuda_gpu_eval_cleanup(gpu_eval_context_t* ctx) {
    if (!ctx) return;

    cuda_eval_context_t* cuda_ctx = (cuda_eval_context_t*)ctx;

    /* Free host buffers */
    if (cuda_ctx->h_boards_converted) free(cuda_ctx->h_boards_converted);
    if (cuda_ctx->h_hole_cards_converted) free(cuda_ctx->h_hole_cards_converted);

    /* Free device memory */
    if (cuda_ctx->d_boards) cudaFree(cuda_ctx->d_boards);
    if (cuda_ctx->d_hole_cards) cudaFree(cuda_ctx->d_hole_cards);
    if (cuda_ctx->d_results) cudaFree(cuda_ctx->d_results);
    if (cuda_ctx->d_wins) cudaFree(cuda_ctx->d_wins);
    if (cuda_ctx->d_ties) cudaFree(cuda_ctx->d_ties);
    if (cuda_ctx->d_rng_states) cudaFree(cuda_ctx->d_rng_states);

    /* Destroy stream */
    if (cuda_ctx->stream) cudaStreamDestroy(cuda_ctx->stream);

    free(cuda_ctx);
}

/* Batch evaluate boards */
int cuda_gpu_eval_batch_boards(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
) {
    if (!ctx || ctx->backend_type != 0) {
        return -1;
    }
    
    cuda_eval_context_t* cuda_ctx = (cuda_eval_context_t*)ctx;
    
    /* Check capacity */
    if (n_boards > cuda_ctx->boards_capacity ||
        n_boards * n_players > cuda_ctx->hole_cards_capacity) {
        return -1;
    }

    /* Convert StdDeck_CardMask to CudaCardMask */
    for (int i = 0; i < n_boards; i++) {
        cuda_ctx->h_boards_converted[i] = convert_to_cuda_cardmask(boards[i]);
    }

    for (int i = 0; i < n_boards * n_players; i++) {
        cuda_ctx->h_hole_cards_converted[i] = convert_to_cuda_cardmask(hole_cards[i]);
    }

    /* Copy converted input data to device */
    cudaError_t err;
    err = cudaMemcpyAsync(cuda_ctx->d_boards, cuda_ctx->h_boards_converted,
                          n_boards * sizeof(CudaCardMask),
                          cudaMemcpyHostToDevice, cuda_ctx->stream);
    if (err != cudaSuccess) return -1;

    err = cudaMemcpyAsync(cuda_ctx->d_hole_cards, cuda_ctx->h_hole_cards_converted,
                          n_boards * n_players * sizeof(CudaCardMask),
                          cudaMemcpyHostToDevice, cuda_ctx->stream);
    if (err != cudaSuccess) return -1;
    
    /* Launch kernel */
    int ret = cuda_eval_batch_boards(
        cuda_ctx->d_boards,
        cuda_ctx->d_hole_cards,
        n_boards,
        n_players,
        cuda_ctx->d_results
    );
    
    if (ret != 0) return ret;
    
    /* Allocate host memory for results if needed */
    if (!result->hand_values) {
        result->hand_values = (HandVal*)malloc(n_boards * n_players * sizeof(HandVal));
        if (!result->hand_values) return -1;
    }
    
    /* Copy results back to host */
    err = cudaMemcpyAsync(result->hand_values, cuda_ctx->d_results,
                          n_boards * n_players * sizeof(HandVal),
                          cudaMemcpyDeviceToHost, cuda_ctx->stream);
    if (err != cudaSuccess) return -1;
    
    /* Wait for all operations to complete */
    err = cudaStreamSynchronize(cuda_ctx->stream);
    if (err != cudaSuccess) return -1;
    
    result->batch_size = n_boards * n_players;
    
    return 0;
}

/* Monte Carlo equity calculation */
int cuda_gpu_monte_carlo_equity(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities
) {
    if (!ctx || ctx->backend_type != 0) {
        return -1;
    }
    
    cuda_eval_context_t* cuda_ctx = (cuda_eval_context_t*)ctx;
    
    /* Check capacity */
    if (n_simulations > cuda_ctx->monte_carlo_capacity || n_players > 10) {
        return -1;
    }
    
    cudaError_t err;
    
    /* Initialize win/tie counters to zero */
    err = cudaMemsetAsync(cuda_ctx->d_wins, 0, n_players * sizeof(int), cuda_ctx->stream);
    if (err != cudaSuccess) return -1;
    
    err = cudaMemsetAsync(cuda_ctx->d_ties, 0, n_players * sizeof(int), cuda_ctx->stream);
    if (err != cudaSuccess) return -1;
    
    /* Initialize RNG states */
    uint32_t* host_rng_states = (uint32_t*)malloc(n_simulations * sizeof(uint32_t));
    if (!host_rng_states) return -1;
    
    for (int i = 0; i < n_simulations; i++) {
        host_rng_states[i] = (uint32_t)(rand() ^ (i * 1234567));
    }
    
    err = cudaMemcpyAsync(cuda_ctx->d_rng_states, host_rng_states,
                          n_simulations * sizeof(uint32_t),
                          cudaMemcpyHostToDevice, cuda_ctx->stream);
    free(host_rng_states);
    if (err != cudaSuccess) return -1;
    
    /* Launch Monte Carlo kernel */
    int ret = cuda_monte_carlo_equity(
        NULL, /* ranges not implemented yet */
        n_players,
        n_simulations,
        cuda_ctx->d_wins,
        cuda_ctx->d_ties,
        cuda_ctx->d_rng_states
    );
    
    if (ret != 0) return ret;
    
    /* Copy results back to host */
    int* host_wins = (int*)malloc(n_players * sizeof(int));
    int* host_ties = (int*)malloc(n_players * sizeof(int));
    if (!host_wins || !host_ties) {
        free(host_wins);
        free(host_ties);
        return -1;
    }
    
    err = cudaMemcpyAsync(host_wins, cuda_ctx->d_wins,
                          n_players * sizeof(int),
                          cudaMemcpyDeviceToHost, cuda_ctx->stream);
    if (err != cudaSuccess) {
        free(host_wins);
        free(host_ties);
        return -1;
    }
    
    err = cudaMemcpyAsync(host_ties, cuda_ctx->d_ties,
                          n_players * sizeof(int),
                          cudaMemcpyDeviceToHost, cuda_ctx->stream);
    if (err != cudaSuccess) {
        free(host_wins);
        free(host_ties);
        return -1;
    }
    
    /* Wait for all operations to complete */
    err = cudaStreamSynchronize(cuda_ctx->stream);
    if (err != cudaSuccess) {
        free(host_wins);
        free(host_ties);
        return -1;
    }
    
    /* Calculate equities */
    for (int p = 0; p < n_players; p++) {
        equities[p] = (float)host_wins[p] / (float)n_simulations + 0.5f * (float)host_ties[p] / (float)n_simulations;
    }
    
    free(host_wins);
    free(host_ties);
    
    return 0;
}

/* Get device information */
int cuda_gpu_get_device_info(int device_id, char* name, size_t* memory, int* compute_units) {
    struct cudaDeviceProp prop;
    cudaError_t err = cudaGetDeviceProperties(&prop, device_id);
    
    if (err != cudaSuccess) {
        return -1;
    }
    
    if (name) {
        snprintf(name, 256, "%s", prop.name);
    }
    
    if (memory) {
        *memory = prop.totalGlobalMem;
    }
    
    if (compute_units) {
        *compute_units = prop.multiProcessorCount;
    }
    
    return 0;
}

/* Check if CUDA is available */
int cuda_gpu_is_available(int backend_type) {
    if (backend_type != 0) { /* Only CUDA in this implementation */
        return 0;
    }
    
    int device_count;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    
    if (err != cudaSuccess || device_count == 0) {
        return 0;
    }
    
    return 1;
}
