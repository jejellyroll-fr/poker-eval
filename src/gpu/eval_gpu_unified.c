/*
 * eval_gpu_unified.c: Unified GPU interface for both CUDA and OpenCL
 */

#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/gpu/eval_gpu.h>

/* Forward declarations for backend-specific functions */
#ifdef HAVE_CUDA
extern gpu_eval_context_t* cuda_gpu_eval_init(int device_id, size_t max_batch_size, int backend_type);
extern gpu_eval_context_t* cuda_gpu_eval_init_game(int device_id, size_t max_batch_size, int backend_type,
                                                   gpu_game_config_t game_config);
extern void cuda_gpu_eval_cleanup(gpu_eval_context_t* ctx);
extern int cuda_gpu_eval_batch_boards(gpu_eval_context_t* ctx, StdDeck_CardMask* boards, 
                                      StdDeck_CardMask* hole_cards, int n_boards, int n_players, 
                                      gpu_eval_result_t* result);
extern int cuda_gpu_eval_batch_boards_hilo(gpu_eval_context_t* ctx, StdDeck_CardMask* boards,
                                           StdDeck_CardMask* hole_cards, int n_boards, int n_players,
                                           gpu_eval_result_hilo_t* result);
extern int cuda_gpu_monte_carlo_equity(gpu_eval_context_t* ctx, StdDeck_CardMask* ranges,
                                       int n_players, int n_simulations, float* equities);
extern int cuda_gpu_get_device_info(int device_id, char* name, size_t* memory, int* compute_units);
extern int cuda_gpu_is_available(int backend_type);
#endif

#ifdef HAVE_OPENCL
#include "opencl/eval_opencl.h"
#endif

/* Unified API implementations */

gpu_eval_context_t* gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t game_config
) {
    switch (backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_eval_init_game(device_id, max_batch_size, backend_type, game_config);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_eval_init_game(device_id, max_batch_size, backend_type, game_config);
#endif
        default:
            return NULL;
    }
}

int gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
) {
    if (!ctx || !result) return -1;
    switch (ctx->backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_eval_batch_boards_hilo(ctx, boards, hole_cards, n_boards, n_players, result);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_eval_batch_boards_hilo(ctx, boards, hole_cards, n_boards, n_players, result);
#endif
        default:
            return -1;
    }
}

int gpu_monte_carlo_equity_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities_hi,
    float* equities_lo,
    float* scoop_equity
) {
    (void)ctx; (void)ranges; (void)n_players; (void)n_simulations;
    (void)equities_hi; (void)equities_lo; (void)scoop_equity;
    return -1;
}

gpu_eval_context_t* gpu_eval_init(int device_id, size_t max_batch_size, int backend_type) {
    switch (backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_eval_init(device_id, max_batch_size, 0);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_eval_init(device_id, max_batch_size, 1);
#endif
        case 2: /* AUTO - try CUDA first, then OpenCL */
#ifdef HAVE_CUDA
            if (cuda_gpu_is_available(0)) {
                gpu_eval_context_t* ctx = cuda_gpu_eval_init(device_id, max_batch_size, 0);
                if (ctx) return ctx;
            }
#endif
#ifdef HAVE_OPENCL
            if (opencl_gpu_is_available(1)) {
                return opencl_gpu_eval_init(device_id, max_batch_size, 1);
            }
#endif
            return NULL;
        default:
            return NULL;
    }
}

void gpu_eval_cleanup(gpu_eval_context_t* ctx) {
    if (!ctx) return;
    
    switch (ctx->backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            cuda_gpu_eval_cleanup(ctx);
            break;
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            opencl_gpu_eval_cleanup(ctx);
            break;
#endif
        default:
            break;
    }
}

int gpu_eval_batch_boards(gpu_eval_context_t* ctx, StdDeck_CardMask* boards, 
                         StdDeck_CardMask* hole_cards, int n_boards, int n_players, 
                         gpu_eval_result_t* result) {
    if (!ctx) return -1;
    
    switch (ctx->backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, result);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, result);
#endif
        default:
            return -1;
    }
}

int gpu_monte_carlo_equity(gpu_eval_context_t* ctx, StdDeck_CardMask* ranges,
                          int n_players, int n_simulations, float* equities) {
    if (!ctx) return -1;
    
    switch (ctx->backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_monte_carlo_equity(ctx, ranges, n_players, n_simulations, equities);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_monte_carlo_equity(ctx, ranges, n_players, n_simulations, equities);
#endif
        default:
            return -1;
    }
}

int gpu_get_device_info(int device_id, char* name, size_t* memory, int* compute_units) {
    /* Try CUDA first, then OpenCL */
#ifdef HAVE_CUDA
    if (cuda_gpu_is_available(0)) {
        return cuda_gpu_get_device_info(device_id, name, memory, compute_units);
    }
#endif
#ifdef HAVE_OPENCL
    if (opencl_gpu_is_available(1)) {
        return opencl_gpu_get_device_info(device_id, name, memory, compute_units);
    }
#endif
    return -1;
}

int gpu_is_available(int backend_type) {
    switch (backend_type) {
#ifdef HAVE_CUDA
        case 0: /* CUDA */
            return cuda_gpu_is_available(backend_type);
#endif
#ifdef HAVE_OPENCL
        case 1: /* OpenCL */
            return opencl_gpu_is_available(backend_type);
#endif
        case 2: /* AUTO - check if any backend is available */
#ifdef HAVE_CUDA
            if (cuda_gpu_is_available(0)) return 1;
#endif
#ifdef HAVE_OPENCL
            if (opencl_gpu_is_available(1)) return 1;
#endif
            return 0;
        default:
            return 0;
    }
}
