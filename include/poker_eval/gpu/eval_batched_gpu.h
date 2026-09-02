/*
 * eval_batched_gpu.h - GPU Batched 7→5 Evaluation Kernels
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * High-performance GPU kernels for batched poker hand evaluation.
 * Supports Hold'em and Omaha with Structure-of-Arrays (SOA) layout
 * for optimal GPU memory access patterns.
 *
 * Features:
 * - Batched 7-card → best 5-card evaluation
 * - Warp-level reductions for maximum throughput
 * - Both CUDA and OpenCL backends
 * - SOA memory layout for coalesced access
 * - Target: billions of boards/minute
 *
 * Priority: ★★★★☆ (section 5.1)
 */

#ifndef POKER_EVAL_GPU_EVAL_BATCHED_H
#define POKER_EVAL_GPU_EVAL_BATCHED_H

#include <poker_eval/core/pokereval_export.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== GPU Backend Types ===== */

typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_CUDA,
    GPU_BACKEND_OPENCL
} gpu_backend_t;

/* ===== GPU Device Info ===== */

typedef struct {
    gpu_backend_t backend;
    int device_id;
    char device_name[256];
    size_t global_mem_size;
    size_t shared_mem_per_block;
    int max_threads_per_block;
    int warp_size;
    int compute_capability_major; /* CUDA only */
    int compute_capability_minor; /* CUDA only */
} gpu_device_info_t;

/* ===== GPU Context ===== */

typedef struct gpu_eval_context_t gpu_eval_context_t;

/* ===== Configuration ===== */

typedef struct {
    gpu_backend_t preferred_backend;
    int device_id;             /* -1 for auto-select */
    size_t max_batch_size;     /* Maximum hands per batch */
    bool enable_profiling;     /* Enable timing/profiling */
    bool verbose;              /* Verbose logging */
} gpu_eval_config_t;

/* ===== Public API ===== */

/**
 * Get default GPU evaluation configuration
 */
POKEREVAL_EXPORT gpu_eval_config_t gpu_eval_default_config(void);

/**
 * Initialize GPU evaluation context
 *
 * @param config  Configuration (or NULL for defaults)
 * @return Context handle, or NULL on failure
 */
POKEREVAL_EXPORT gpu_eval_context_t* gpu_eval_init_batched(const gpu_eval_config_t* config);

/**
 * Free GPU evaluation context
 *
 * @param ctx  Context to free
 */
POKEREVAL_EXPORT void gpu_eval_free(gpu_eval_context_t* ctx);

/**
 * Get GPU device information
 *
 * @param ctx   GPU context
 * @param info  Device info (output)
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_get_device_info(
    const gpu_eval_context_t* ctx,
    gpu_device_info_t* info
);

/**
 * Evaluate batch of 7-card hands (Hold'em style)
 *
 * Input: Arrays of 7 cards per hand (2 hole + 5 board)
 * Output: HandVal for each hand
 *
 * @param ctx         GPU context
 * @param hands       Array of card indices [batch_size * 7]
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_holdem_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha5 hands (5 hole + 5 board)
 *
 * @param ctx         GPU context
 * @param hole        Array of 5 hole cards per hand [batch_size * 5]
 * @param board       Array of 5 board cards per hand [batch_size * 5]
 * @param batch_size  Number of hands
 * @param out_values  Output hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_omaha5_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha6 hands (6 hole + 5 board)
 *
 * @param ctx         GPU context
 * @param hole        Array of 6 hole cards per hand [batch_size * 6]
 * @param board       Array of 5 board cards per hand [batch_size * 5]
 * @param batch_size  Number of hands
 * @param out_values  Output hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_omaha6_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha hands (4 hole + 5 board)
 *
 * Each hand uses exactly 2 of 4 hole cards + 3 of 5 board cards
 *
 * @param ctx         GPU context
 * @param hole        Array of 4 hole cards per hand [batch_size * 4]
 * @param board       Array of 5 board cards per hand [batch_size * 5]
 * @param batch_size  Number of hands
 * @param out_values  Output hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_omaha_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
);

/* ===== Stud / Razz / Hi-Lo Batch API ===== */

/**
 * Evaluate batch of 7-card Stud hands (high evaluation)
 *
 * Same C(7,5)=21 combination logic as Hold'em but without
 * a separate board — all 7 cards belong to the hand.
 *
 * @param ctx         GPU context
 * @param hands       Array of 7 card indices per hand [batch_size * 7]
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_stud_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of 7-card Razz hands (A-5 low evaluation)
 *
 * Uses low lookup tables (bottomFiveCardsTable, bottomCardTable).
 * Lower values are better. Best hand: A-2-3-4-5 (wheel).
 *
 * @param ctx         GPU context
 * @param hands       Array of 7 card indices per hand [batch_size * 7]
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output low hand values [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_razz_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of hands with Hi/Lo split (generic)
 *
 * Evaluates both high and low for any game type that supports it
 * (Stud8, Holdem8, Omaha8, etc.). Uses gpu_game_type_t to select
 * the evaluation logic.
 *
 * @param ctx              GPU context
 * @param hands            Array of card indices [batch_size * cards_per_hand]
 * @param batch_size       Number of hands to evaluate
 * @param cards_per_hand   Number of cards per hand (7 for Stud8, 9 for Omaha8, etc.)
 * @param game_type        Game type (from gpu_game_type_t enum)
 * @param out_hi_values    Output high hand values [batch_size]
 * @param out_lo_values    Output low hand values [batch_size]
 * @param out_lo_qualifies Output low qualification flags [batch_size] (1=qualifies, 0=no low)
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_hilo_batch(
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
 * Evaluate batch of Omaha Hi/Lo hands (4 hole + 5 board)
 *
 * Specialized Omaha8 evaluation: must use exactly 2 of 4 hole cards
 * + 3 of 5 board cards for both high and low.
 *
 * @param ctx              GPU context
 * @param hands            Array of 9 card indices per hand [batch_size * 9]
 *                         (4 hole cards followed by 5 board cards)
 * @param batch_size       Number of hands to evaluate
 * @param out_hi_values    Output high hand values [batch_size]
 * @param out_lo_values    Output low hand values [batch_size]
 * @param out_lo_qualifies Output low qualification flags [batch_size]
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_omaha_hilo_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
);

/**
 * Evaluate batch with equity calculation (Hold'em heads-up)
 *
 * Evaluates all boards and computes win/tie/loss counts
 *
 * @param ctx           GPU context
 * @param hero_hand     Hero's 2 hole cards
 * @param villain_hand  Villain's 2 hole cards
 * @param boards        Array of 5-card boards [num_boards * 5]
 * @param num_boards    Number of boards
 * @param out_wins      Number of wins for hero
 * @param out_ties      Number of ties
 * @param out_losses    Number of losses
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int gpu_eval_equity_holdem(
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
 * Get performance statistics
 *
 * @param ctx                GPU context
 * @param out_total_evals    Total evaluations performed
 * @param out_total_time_ms  Total GPU time (milliseconds)
 * @param out_evals_per_sec  Current throughput (evals/second)
 */
POKEREVAL_EXPORT void gpu_eval_get_stats(
    const gpu_eval_context_t* ctx,
    uint64_t* out_total_evals,
    double* out_total_time_ms,
    double* out_evals_per_sec
);

/**
 * Reset performance statistics
 *
 * @param ctx  GPU context
 */
POKEREVAL_EXPORT void gpu_eval_reset_stats(gpu_eval_context_t* ctx);

/**
 * Enable/disable concurrent-stream pipelining on the CUDA backend.
 *
 * When enabled (the default), large batches are split across the backend's
 * concurrent streams so that host-to-device copies, kernel compute, and
 * device-to-host copies overlap instead of serialising on one stream. Small
 * batches fall back to a single-stream path where the overlap would add
 * nothing but scheduling overhead.
 *
 * @param ctx     GPU context
 * @param enable  true to pipeline, false to force the inline single-stream path
 * @return 0 on success, non-zero if the active backend does not support it
 */
POKEREVAL_EXPORT int gpu_eval_enable_streaming(
    gpu_eval_context_t* ctx,
    bool enable
);

/* ===== Helper Functions ===== */

/**
 * Check if GPU evaluation is available
 *
 * @return true if at least one GPU backend is available
 */
POKEREVAL_EXPORT bool gpu_eval_is_available(void);

/**
 * Get number of available GPU devices
 *
 * @param backend  Backend type
 * @return Number of devices, or -1 on error
 */
POKEREVAL_EXPORT int gpu_eval_get_device_count(gpu_backend_t backend);

/**
 * Get recommended batch size for device
 *
 * @param ctx  GPU context
 * @return Recommended batch size (hands)
 */
POKEREVAL_EXPORT size_t gpu_eval_get_recommended_batch_size(
    const gpu_eval_context_t* ctx
);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GPU_EVAL_BATCHED_H */
