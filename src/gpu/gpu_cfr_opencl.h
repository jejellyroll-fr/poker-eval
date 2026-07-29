/*
 * gpu_cfr_opencl.h - OpenCL backend interface for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Internal header for OpenCL backend implementation of GPU-CFR.
 * Provides cross-platform GPU support for AMD, Intel, and NVIDIA GPUs.
 */

#ifndef POKER_EVAL_GPU_CFR_OPENCL_H
#define POKER_EVAL_GPU_CFR_OPENCL_H

#include <poker_eval/gpu/gpu_cfr.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of OpenCL CFR context */
typedef struct gpu_cfr_opencl_context_t gpu_cfr_opencl_context_t;

/**
 * Initialize GPU-CFR OpenCL backend
 *
 * @param config  Configuration parameters
 * @return Opaque context pointer or NULL on failure
 */
gpu_cfr_opencl_context_t* gpu_cfr_init_opencl(const gpu_cfr_config_t* config);

/**
 * Free GPU-CFR OpenCL resources
 *
 * @param ctx  Context to free
 */
void gpu_cfr_free_opencl(gpu_cfr_opencl_context_t* ctx);

/**
 * Load CFR state from host to GPU
 *
 * @param ctx      GPU-CFR context
 * @param storage  Matrix storage (host memory)
 * @return 0 on success, negative on error
 */
int gpu_cfr_load_state_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const cfr_matrix_storage_t* storage
);

/**
 * Download CFR state from GPU to host
 *
 * @param ctx      GPU-CFR context
 * @param storage  Output matrix storage (host memory)
 * @return 0 on success, negative on error
 */
int gpu_cfr_download_state_opencl(
    gpu_cfr_opencl_context_t* ctx,
    cfr_matrix_storage_t* storage
);

/**
 * Run CFR iterations on GPU using OpenCL
 *
 * Performs GPU-accelerated CFR solving:
 * 1. Regret matching to compute current strategy
 * 2. Strategy accumulation
 * 3. Regret update with discounting
 *
 * @param ctx         GPU-CFR context
 * @param iterations  Number of iterations to run
 * @return 0 on success, negative on error
 */
int gpu_cfr_solve_opencl(
    gpu_cfr_opencl_context_t* ctx,
    int iterations
);

/**
 * Load sparse transition matrix to GPU
 *
 * @param ctx     GPU-CFR context
 * @param matrix  Sparse CSR matrix
 * @return 0 on success, negative on error
 */
int gpu_cfr_load_sparse_matrix_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const sparse_matrix_csr_t* matrix
);

/**
 * Load regret deltas to GPU
 *
 * Called after CPU game tree traversal to upload computed deltas.
 *
 * @param ctx     GPU-CFR context
 * @param deltas  Regret deltas [num_infosets × max_actions]
 * @return 0 on success, negative on error
 */
int gpu_cfr_load_deltas_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const float* deltas
);

/**
 * Get GPU-CFR statistics
 *
 * @param ctx    GPU-CFR context
 * @param stats  Output statistics
 * @return 0 on success, negative on error
 */
int gpu_cfr_get_stats_opencl(
    const gpu_cfr_opencl_context_t* ctx,
    gpu_cfr_stats_t* stats
);

/**
 * Reset CFR state (zero all regrets and strategies)
 *
 * @param ctx  GPU-CFR context
 * @return 0 on success, negative on error
 */
int gpu_cfr_reset_opencl(gpu_cfr_opencl_context_t* ctx);

/**
 * Get device name string
 *
 * @param ctx  GPU-CFR context
 * @return Device name string (static, do not free)
 */
const char* gpu_cfr_get_device_name_opencl(const gpu_cfr_opencl_context_t* ctx);

/**
 * Get number of available OpenCL GPU devices
 *
 * @return Number of available GPUs
 */
int gpu_cfr_get_device_count_opencl(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GPU_CFR_OPENCL_H */
