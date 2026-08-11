/*
 * eval_cuda_generic.c: C wrapper for generic multi-game CUDA kernels
 *
 * This file provides the C interface to the generic GPU evaluation kernels
 * that support multiple poker variants.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cuda_runtime.h>
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/core/poker_defs.h>

/* CUDA-compatible card mask structure */
typedef struct {
    uint32_t cards_n;
    uint32_t cards[4]; /* spades, hearts, diamonds, clubs */
} CudaCardMask;

/* External kernel launch function */
extern int cuda_eval_generic_boards(
    void* d_boards,
    void* d_hole_cards,
    int n_boards,
    int n_players,
    gpu_game_config_t config,
    void* d_results_hi,
    void* d_results_lo,
    void* d_lo_qualifies
);

/* Extended CUDA context structure */
typedef struct {
    gpu_eval_context_t base;
    cudaStream_t stream;
    void* d_boards;
    void* d_hole_cards;
    void* d_results_hi;
    void* d_results_lo;
    void* d_lo_qualifies;
    CudaCardMask* h_boards_converted;
    CudaCardMask* h_hole_cards_converted;
    size_t boards_capacity;
    size_t hole_cards_capacity;
    size_t results_capacity;
} cuda_eval_context_generic_t;

/* Forward declarations needed for warning-free builds */
gpu_eval_context_t* cuda_gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t game_config
);

int cuda_gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
);

/* Convert StdDeck_CardMask to CudaCardMask */
static CudaCardMask convert_to_cuda_cardmask(StdDeck_CardMask std_mask) {
    CudaCardMask cuda_mask;
    cuda_mask.cards_n = 0;
    cuda_mask.cards[0] = StdDeck_CardMask_SPADES(std_mask);
    cuda_mask.cards[1] = StdDeck_CardMask_HEARTS(std_mask);
    cuda_mask.cards[2] = StdDeck_CardMask_DIAMONDS(std_mask);
    cuda_mask.cards[3] = StdDeck_CardMask_CLUBS(std_mask);
    return cuda_mask;
}

/*
 * Initialize GPU context with game configuration
 */
gpu_eval_context_t* cuda_gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t game_config
) {
    if (backend_type != 0) { /* Only CUDA for now */
        return NULL;
    }

    cuda_eval_context_generic_t* ctx = (cuda_eval_context_generic_t*)calloc(
        1, sizeof(cuda_eval_context_generic_t));
    if (!ctx) {
        return NULL;
    }

    /* Set CUDA device */
    cudaError_t err = cudaSetDevice(device_id);
    if (err != cudaSuccess) {
        free(ctx);
        return NULL;
    }

    /* Initialize base context */
    ctx->base.device_id = device_id;
    ctx->base.max_batch_size = max_batch_size;
    ctx->base.backend_type = 0;
    ctx->base.game_config = game_config;

    /* Create CUDA stream */
    err = cudaStreamCreate(&ctx->stream);
    if (err != cudaSuccess) {
        free(ctx);
        return NULL;
    }

    /* Allocate device memory */
    size_t board_size = max_batch_size * sizeof(CudaCardMask);
    size_t hole_cards_size = max_batch_size * 10 * sizeof(CudaCardMask);
    size_t results_size = max_batch_size * 10 * sizeof(HandVal);

    cudaMalloc(&ctx->d_boards, board_size);
    cudaMalloc(&ctx->d_hole_cards, hole_cards_size);
    cudaMalloc(&ctx->d_results_hi, results_size);

    if (game_config.eval_low) {
        cudaMalloc(&ctx->d_results_lo, results_size);
        cudaMalloc(&ctx->d_lo_qualifies, max_batch_size * 10 * sizeof(int));
    }

    /* Allocate host conversion buffers */
    ctx->h_boards_converted = (CudaCardMask*)malloc(board_size);
    ctx->h_hole_cards_converted = (CudaCardMask*)malloc(hole_cards_size);
    if (!ctx->h_boards_converted || !ctx->h_hole_cards_converted) {
        free(ctx->h_boards_converted);
        free(ctx->h_hole_cards_converted);
        free(ctx);
        return NULL;
    }

    ctx->boards_capacity = max_batch_size;
    ctx->hole_cards_capacity = max_batch_size * 10;
    ctx->results_capacity = max_batch_size * 10;

    return (gpu_eval_context_t*)ctx;
}

/*
 * Batch evaluate with hi/lo support
 */
int cuda_gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
) {
    if (!ctx || ctx->backend_type != 0) {
        return -1;
    }

    cuda_eval_context_generic_t* cuda_ctx = (cuda_eval_context_generic_t*)ctx;
    gpu_game_config_t config = ctx->game_config;

    /* Convert cards to CUDA format */
    for (int i = 0; i < n_boards; i++) {
        cuda_ctx->h_boards_converted[i] = convert_to_cuda_cardmask(boards[i]);
    }

    for (int i = 0; i < n_boards * n_players; i++) {
        cuda_ctx->h_hole_cards_converted[i] = convert_to_cuda_cardmask(hole_cards[i]);
    }

    /* Copy to device */
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
        int ret = cuda_eval_generic_boards(
        cuda_ctx->d_boards,
        cuda_ctx->d_hole_cards,
        n_boards,
        n_players,
        config,
        cuda_ctx->d_results_hi,
        cuda_ctx->d_results_lo,
        cuda_ctx->d_lo_qualifies
    );

    if (ret != 0) return ret;

    /* Allocate result arrays if needed */
    if (!result->hand_values_hi) {
        result->hand_values_hi = (HandVal*)malloc(n_boards * n_players * sizeof(HandVal));
        if (!result->hand_values_hi)
            return -1;
    }

    if (config.eval_low && !result->hand_values_lo) {
        result->hand_values_lo = (HandVal*)malloc(n_boards * n_players * sizeof(HandVal));
        result->lo_qualifies = (int*)malloc(n_boards * n_players * sizeof(int));
        if (!result->hand_values_lo || !result->lo_qualifies) {
            free(result->hand_values_lo);
            free(result->lo_qualifies);
            return -1;
        }
    }

    /* Copy results back */
    err = cudaMemcpyAsync(result->hand_values_hi, cuda_ctx->d_results_hi,
                          n_boards * n_players * sizeof(HandVal),
                          cudaMemcpyDeviceToHost, cuda_ctx->stream);
    if (err != cudaSuccess) return -1;

    if (config.eval_low) {
        err = cudaMemcpyAsync(result->hand_values_lo, cuda_ctx->d_results_lo,
                              n_boards * n_players * sizeof(HandVal),
                              cudaMemcpyDeviceToHost, cuda_ctx->stream);
        if (err != cudaSuccess) return -1;

        err = cudaMemcpyAsync(result->lo_qualifies, cuda_ctx->d_lo_qualifies,
                              n_boards * n_players * sizeof(int),
                              cudaMemcpyDeviceToHost, cuda_ctx->stream);
        if (err != cudaSuccess) return -1;
    }

    /* Wait for completion */
    err = cudaStreamSynchronize(cuda_ctx->stream);
    if (err != cudaSuccess) return -1;

    result->batch_size = n_boards * n_players;

    return 0;
}
