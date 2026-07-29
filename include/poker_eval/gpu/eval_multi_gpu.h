/*
 * eval_multi_gpu.h - Multi-GPU support for batched evaluation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * API for distributing work across multiple GPUs for maximum throughput.
 */

#ifndef POKER_EVAL_MULTI_GPU_H
#define POKER_EVAL_MULTI_GPU_H

#include "eval_batched_gpu.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Multi-GPU context
 *
 * Manages multiple GPU devices and distributes work across them.
 */
typedef struct multi_gpu_context_t multi_gpu_context_t;

/**
 * Work distribution strategy
 */
typedef enum {
    MULTI_GPU_ROUND_ROBIN,     /* Distribute batches in round-robin */
    MULTI_GPU_STATIC_SPLIT,    /* Split batch statically across GPUs */
    MULTI_GPU_DYNAMIC_BALANCE, /* Dynamic load balancing based on perf */
} multi_gpu_strategy_t;

/**
 * Multi-GPU configuration
 */
typedef struct {
    int num_devices;                    /* Number of GPUs to use (-1 = all) */
    int* device_ids;                    /* Specific device IDs (NULL = use first N) */
    multi_gpu_strategy_t strategy;      /* Work distribution strategy */
    gpu_backend_t preferred_backend;    /* CUDA or OpenCL */
    size_t batch_size_per_gpu;          /* Max batch size per GPU */
    bool enable_profiling;              /* Enable performance tracking */
    bool verbose;                       /* Print device info */
} multi_gpu_config_t;

/**
 * Multi-GPU statistics
 */
typedef struct {
    uint64_t total_evals;              /* Total hands evaluated */
    double total_time_ms;               /* Total time across all GPUs */
    double* per_gpu_time_ms;            /* Time per GPU [num_devices] */
    uint64_t* per_gpu_evals;            /* Evals per GPU [num_devices] */
    double aggregate_throughput;        /* Hands/sec (all GPUs combined) */
} multi_gpu_stats_t;

/**
 * Get default multi-GPU configuration
 *
 * @return Default configuration (all available GPUs, round-robin)
 */
multi_gpu_config_t multi_gpu_default_config(void);

/**
 * Initialize multi-GPU context
 *
 * Creates GPU contexts for each device and prepares for evaluation.
 *
 * @param config  Configuration (NULL for defaults)
 * @return Multi-GPU context or NULL on failure
 */
multi_gpu_context_t* multi_gpu_init(const multi_gpu_config_t* config);

/**
 * Free multi-GPU context
 *
 * @param ctx  Context to free
 */
void multi_gpu_free(multi_gpu_context_t* ctx);

/**
 * Evaluate batch of Hold'em hands across multiple GPUs
 *
 * Automatically distributes work across available GPUs based on strategy.
 *
 * @param ctx         Multi-GPU context
 * @param hands       Array of 7-card hands [batch_size * 7]
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int multi_gpu_eval_holdem_batch(
    multi_gpu_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Evaluate batch of Omaha hands across multiple GPUs
 *
 * @param ctx         Multi-GPU context
 * @param hands       Array of 9-card hands [batch_size * 9] (4 hole + 5 board)
 * @param batch_size  Number of hands to evaluate
 * @param out_values  Output array for HandVal results [batch_size]
 * @return 0 on success, negative on error
 */
int multi_gpu_eval_omaha_batch(
    multi_gpu_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
);

/**
 * Get multi-GPU statistics
 *
 * @param ctx    Multi-GPU context
 * @param stats  Output statistics structure
 * @return 0 on success, negative on error
 */
int multi_gpu_get_stats(
    const multi_gpu_context_t* ctx,
    multi_gpu_stats_t* stats
);

/**
 * Reset multi-GPU statistics
 *
 * @param ctx  Multi-GPU context
 */
void multi_gpu_reset_stats(multi_gpu_context_t* ctx);

/**
 * Get number of active GPUs in context
 *
 * @param ctx  Multi-GPU context
 * @return Number of GPUs or 0 on error
 */
int multi_gpu_get_device_count(const multi_gpu_context_t* ctx);

/**
 * Get info for specific GPU in multi-GPU context
 *
 * @param ctx        Multi-GPU context
 * @param device_idx Device index (0 to num_devices-1)
 * @param info       Output device info
 * @return 0 on success, negative on error
 */
int multi_gpu_get_device_info(
    const multi_gpu_context_t* ctx,
    int device_idx,
    gpu_device_info_t* info
);

/**
 * Check if multi-GPU is available
 *
 * @return Number of available GPUs (>= 2 means multi-GPU possible)
 */
int multi_gpu_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_MULTI_GPU_H */
