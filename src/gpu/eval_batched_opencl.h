/*
 * eval_batched_opencl.h - OpenCL backend interface
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Internal header for OpenCL backend implementation.
 * Not exposed to public API - use eval_batched_gpu.h instead.
 */

#ifndef POKER_EVAL_BATCHED_OPENCL_H
#define POKER_EVAL_BATCHED_OPENCL_H

#include <poker_eval/gpu/eval_batched_gpu.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of OpenCL context */
typedef struct gpu_eval_context_opencl_t gpu_eval_context_opencl_t;

/**
 * Initialize OpenCL backend
 *
 * @param config  Configuration parameters
 * @return Opaque context pointer or NULL on failure
 */
gpu_eval_context_t* gpu_eval_init_opencl(const gpu_eval_config_t* config);

/**
 * Free OpenCL resources
 *
 * @param ctx  Context to free
 */
void gpu_eval_free_opencl(gpu_eval_context_t* ctx);

/**
 * Evaluate batch of Hold'em hands (OpenCL implementation)
 *
 * @param ctx         GPU context
 * @param hands       Array of 7-card hands [batch_size * 7]
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int gpu_eval_holdem_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha-4 hands (OpenCL implementation)
 *
 * @param ctx         GPU context
 * @param hands       Array of 9-card hands [batch_size * 9] (4 hole + 5 board)
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int gpu_eval_omaha_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha-5 hands (OpenCL implementation)
 *
 * @param ctx         GPU context
 * @param hands       Array of 10-card hands [batch_size * 10] (5 hole + 5 board)
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int gpu_eval_omaha5_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha-6 hands (OpenCL implementation)
 *
 * @param ctx         GPU context
 * @param hands       Array of 11-card hands [batch_size * 11] (6 hole + 5 board)
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int gpu_eval_omaha6_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/* ===== Stud / Razz / Hi-Lo OpenCL Backend ===== */

/**
 * Evaluate batch of 7-card Stud hands (OpenCL, high evaluation)
 */
int gpu_eval_stud_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of 7-card Razz hands (OpenCL, A-5 low evaluation)
 */
int gpu_eval_razz_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch with Hi/Lo split (OpenCL, generic game type)
 */
int gpu_eval_hilo_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
);

/**
 * Evaluate batch of Omaha Hi/Lo hands (OpenCL, 4 hole + 5 board)
 */
int gpu_eval_omaha_hilo_batch_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
);

/**
 * Hold'em equity calculation (OpenCL implementation)
 *
 * @param ctx           GPU context
 * @param hero_hand     Hero's 2 hole cards
 * @param villain_hand  Villain's 2 hole cards
 * @param boards        Array of 5-card boards [num_boards * 5]
 * @param num_boards    Number of boards to evaluate
 * @param out_wins      Output: hero win count
 * @param out_ties      Output: tie count
 * @param out_losses    Output: hero loss count
 * @return 0 on success, negative on error
 */
int gpu_eval_equity_holdem_opencl(
    gpu_eval_context_t* ctx,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses
);

/**
 * Get last kernel execution time (OpenCL)
 *
 * @param ctx  GPU context
 * @return Execution time in milliseconds
 */
double gpu_eval_get_last_time_opencl(gpu_eval_context_t* ctx);

/**
 * Get device name string (OpenCL)
 *
 * @param ctx  GPU context
 * @return Device name string (static, do not free)
 */
const char* gpu_eval_get_device_name_opencl(gpu_eval_context_t* ctx);

/**
 * Populate gpu_device_info_t structure using OpenCL context data.
 *
 * @param ctx   GPU context
 * @param info  Output structure
 */
void gpu_eval_fill_device_info_opencl(gpu_eval_context_t* ctx, gpu_device_info_t* info);

/**
 * Get OpenCL device count
 *
 * @return Number of available OpenCL devices
 */
int gpu_eval_get_device_count_opencl(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_BATCHED_OPENCL_H */
