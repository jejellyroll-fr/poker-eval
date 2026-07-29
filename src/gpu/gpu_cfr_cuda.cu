/*
 * gpu_cfr_cuda.c - CUDA backend for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * GPU memory management and kernel launches for CFR solving.
 */

#include "poker_eval/gpu/gpu_cfr.h"
#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* CUDA error checking */
#define CUDA_CHECK(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, \
                cudaGetErrorString(err)); \
        return -1; \
    } \
} while(0)

#define CUDA_CHECK_VOID(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, \
                cudaGetErrorString(err)); \
        return; \
    } \
} while(0)

/* Kernel launch declarations */
extern "C" {
    void launch_regret_matching(
        const float* d_regrets,
        float* d_curr_strategy,
        const uint8_t* d_action_counts,
        int num_infosets,
        int max_actions,
        cudaStream_t stream
    );

    void launch_regret_update(
        float alpha,
        float* d_regrets,
        const float* d_deltas,
        int size,
        cudaStream_t stream
    );

    void launch_strategy_accumulate(
        float weight,
        float* d_avg_strategy,
        const float* d_curr_strategy,
        int size,
        cudaStream_t stream
    );

    void launch_spmv_csr(
        const int* d_row_ptr,
        const int* d_col_idx,
        const float* d_values,
        const float* d_x,
        float* d_y,
        int num_rows,
        cudaStream_t stream
    );

    void launch_zero_array(
        float* d_data,
        int size,
        cudaStream_t stream
    );

    void launch_normalize_strategy(
        float* d_strategy,
        const uint8_t* d_action_counts,
        int num_infosets,
        int max_actions,
        cudaStream_t stream
    );
}

/* Extended GPU-CFR context with CUDA-specific fields */
typedef struct {
    gpu_cfr_config_t config;
    cfr_matrix_storage_t* host_storage;

    /* CUDA device pointers */
    float* d_regrets;
    float* d_avg_strategy;
    float* d_curr_strategy;
    float* d_deltas;  /* Temporary buffer for regret deltas */
    uint8_t* d_action_counts;

    /* Sparse matrix (CSR) */
    int* d_csr_row_ptr;
    int* d_csr_col_idx;
    float* d_csr_values;

    /* CUDA stream and events */
    cudaStream_t stream;
    cudaEvent_t start_event;
    cudaEvent_t stop_event;

    /* Statistics */
    gpu_cfr_stats_t stats;
} gpu_cfr_cuda_context_t;

/* Timing utility */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* ===== CUDA Memory Management ===== */

int gpu_cfr_cuda_init(gpu_cfr_context_t** out_ctx, const gpu_cfr_config_t* config) {
    if (!out_ctx || !config) return -1;

    gpu_cfr_cuda_context_t* ctx =
        (gpu_cfr_cuda_context_t*)calloc(1, sizeof(gpu_cfr_cuda_context_t));
    if (!ctx) return -1;

    ctx->config = *config;

    /* Set CUDA device */
    int device_id = config->device_id;
    if (device_id < 0) device_id = 0;

    CUDA_CHECK(cudaSetDevice(device_id));

    /* Create stream and events */
    CUDA_CHECK(cudaStreamCreate(&ctx->stream));
    CUDA_CHECK(cudaEventCreate(&ctx->start_event));
    CUDA_CHECK(cudaEventCreate(&ctx->stop_event));

    /* Allocate host storage */
    ctx->host_storage = cfr_matrix_storage_create(
        config->num_infosets,
        config->max_actions
    );

    if (!ctx->host_storage) {
        free(ctx);
        return -1;
    }

    /* Allocate device memory */
    size_t matrix_size = config->num_infosets * config->max_actions;
    size_t matrix_bytes = matrix_size * sizeof(float);
    size_t counts_bytes = config->num_infosets * sizeof(uint8_t);

    CUDA_CHECK(cudaMalloc(&ctx->d_regrets, matrix_bytes));
    CUDA_CHECK(cudaMalloc(&ctx->d_avg_strategy, matrix_bytes));
    CUDA_CHECK(cudaMalloc(&ctx->d_curr_strategy, matrix_bytes));
    CUDA_CHECK(cudaMalloc(&ctx->d_deltas, matrix_bytes));
    CUDA_CHECK(cudaMalloc(&ctx->d_action_counts, counts_bytes));

    /* Zero out device matrices */
    CUDA_CHECK(cudaMemset(ctx->d_regrets, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_avg_strategy, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_curr_strategy, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_deltas, 0, matrix_bytes));

    /* Initialize action counts to max_actions by default */
    /* (will be overwritten when loading real state) */
    uint8_t* temp_counts = (uint8_t*)malloc(counts_bytes);
    for (int i = 0; i < config->num_infosets; i++) {
        temp_counts[i] = config->max_actions;
    }
    CUDA_CHECK(cudaMemcpy(ctx->d_action_counts, temp_counts,
                          counts_bytes, cudaMemcpyHostToDevice));
    free(temp_counts);

    if (config->verbose) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, device_id);

        printf("GPU-CFR CUDA initialized:\n");
        printf("  Device: %s\n", prop.name);
        printf("  Infosets: %d\n", config->num_infosets);
        printf("  Max actions: %d\n", config->max_actions);
        printf("  Matrix size: %d × %d\n", config->num_infosets, config->max_actions);
        printf("  GPU Memory allocated: %.2f MB\n",
               (4 * matrix_bytes + counts_bytes) / (1024.0 * 1024.0));
    }

    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    *out_ctx = (gpu_cfr_context_t*)ctx;
    return 0;
}

void gpu_cfr_cuda_free(gpu_cfr_context_t* ctx_opaque) {
    if (!ctx_opaque) return;

    gpu_cfr_cuda_context_t* ctx = (gpu_cfr_cuda_context_t*)ctx_opaque;

    /* Free device memory */
    if (ctx->d_regrets) cudaFree(ctx->d_regrets);
    if (ctx->d_avg_strategy) cudaFree(ctx->d_avg_strategy);
    if (ctx->d_curr_strategy) cudaFree(ctx->d_curr_strategy);
    if (ctx->d_deltas) cudaFree(ctx->d_deltas);
    if (ctx->d_action_counts) cudaFree(ctx->d_action_counts);

    if (ctx->d_csr_row_ptr) cudaFree(ctx->d_csr_row_ptr);
    if (ctx->d_csr_col_idx) cudaFree(ctx->d_csr_col_idx);
    if (ctx->d_csr_values) cudaFree(ctx->d_csr_values);

    /* Destroy stream and events */
    cudaStreamDestroy(ctx->stream);
    cudaEventDestroy(ctx->start_event);
    cudaEventDestroy(ctx->stop_event);

    /* Free host storage */
    cfr_matrix_storage_free(ctx->host_storage);

    free(ctx);
}

/* ===== State Transfer ===== */

int gpu_cfr_cuda_load_state(
    gpu_cfr_context_t* ctx_opaque,
    const cfr_matrix_storage_t* storage
) {
    if (!ctx_opaque || !storage) return -1;

    gpu_cfr_cuda_context_t* ctx = (gpu_cfr_cuda_context_t*)ctx_opaque;

    size_t matrix_size = storage->num_infosets * storage->max_actions;
    size_t matrix_bytes = matrix_size * sizeof(float);
    size_t counts_bytes = storage->num_infosets * sizeof(uint8_t);

    /* Upload to device */
    CUDA_CHECK(cudaMemcpy(ctx->d_regrets, storage->regrets,
                          matrix_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(ctx->d_avg_strategy, storage->avg_strategy,
                          matrix_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(ctx->d_action_counts, storage->action_counts,
                          counts_bytes, cudaMemcpyHostToDevice));

    /* Copy to host storage as well */
    memcpy(ctx->host_storage->regrets, storage->regrets, matrix_bytes);
    memcpy(ctx->host_storage->avg_strategy, storage->avg_strategy, matrix_bytes);
    memcpy(ctx->host_storage->action_counts, storage->action_counts, counts_bytes);

    return 0;
}

int gpu_cfr_cuda_download_state(
    gpu_cfr_context_t* ctx_opaque,
    cfr_matrix_storage_t* storage
) {
    if (!ctx_opaque || !storage) return -1;

    gpu_cfr_cuda_context_t* ctx = (gpu_cfr_cuda_context_t*)ctx_opaque;

    size_t matrix_size = ctx->config.num_infosets * ctx->config.max_actions;
    size_t matrix_bytes = matrix_size * sizeof(float);

    /* Download from device */
    CUDA_CHECK(cudaMemcpy(storage->regrets, ctx->d_regrets,
                          matrix_bytes, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(storage->avg_strategy, ctx->d_avg_strategy,
                          matrix_bytes, cudaMemcpyDeviceToHost));

    return 0;
}

/* ===== CFR Solving ===== */

int gpu_cfr_cuda_solve(
    gpu_cfr_context_t* ctx_opaque,
    int iterations
) {
    if (!ctx_opaque || iterations <= 0) return -1;

    gpu_cfr_cuda_context_t* ctx = (gpu_cfr_cuda_context_t*)ctx_opaque;

    if (ctx->config.verbose) {
        printf("Running %d GPU-CFR iterations on CUDA...\n", iterations);
    }

    double total_start = get_time_ms();

    int matrix_size = ctx->config.num_infosets * ctx->config.max_actions;

    for (int iter = 0; iter < iterations; iter++) {
        /* 1. Compute current strategy: π = regret_matching(R) */
        if (ctx->config.enable_profiling) {
            cudaEventRecord(ctx->start_event, ctx->stream);
        }

        launch_regret_matching(
            ctx->d_regrets,
            ctx->d_curr_strategy,
            ctx->d_action_counts,
            ctx->config.num_infosets,
            ctx->config.max_actions,
            ctx->stream
        );

        if (ctx->config.enable_profiling) {
            cudaEventRecord(ctx->stop_event, ctx->stream);
            cudaEventSynchronize(ctx->stop_event);

            float elapsed_ms = 0;
            cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
            ctx->stats.strategy_compute_time_ms += elapsed_ms;
        }

        /* 2. TODO: Compute regret deltas (game tree traversal) */
        /* For now, use placeholder: random deltas */
        /* In real implementation, this would call game adapter */

        /* Zero deltas buffer */
        launch_zero_array(ctx->d_deltas, matrix_size, ctx->stream);

        /* 3. Update regrets: R' = discount * R + Δ */
        if (ctx->config.enable_profiling) {
            cudaEventRecord(ctx->start_event, ctx->stream);
        }

        launch_regret_update(
            ctx->config.regret_discount,
            ctx->d_regrets,
            ctx->d_deltas,
            matrix_size,
            ctx->stream
        );

        if (ctx->config.enable_profiling) {
            cudaEventRecord(ctx->stop_event, ctx->stream);
            cudaEventSynchronize(ctx->stop_event);

            float elapsed_ms = 0;
            cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
            ctx->stats.regret_update_time_ms += elapsed_ms;
        }

        /* 4. Update average strategy: S' = S + weight * π */
        launch_strategy_accumulate(
            ctx->config.strategy_weight,
            ctx->d_avg_strategy,
            ctx->d_curr_strategy,
            matrix_size,
            ctx->stream
        );

        /* Progress logging */
        if (ctx->config.verbose && (iter % 100 == 0)) {
            printf("  Iteration %d/%d\n", iter, iterations);
        }
    }

    /* Synchronize to ensure all kernels complete */
    cudaStreamSynchronize(ctx->stream);

    double total_elapsed = get_time_ms() - total_start;

    /* Update statistics */
    ctx->stats.iterations_completed += iterations;
    ctx->stats.total_time_ms += total_elapsed;
    ctx->stats.avg_iteration_time_ms = ctx->stats.total_time_ms / ctx->stats.iterations_completed;

    if (ctx->config.verbose) {
        printf("Completed %d iterations in %.2f ms (avg %.3f ms/iter)\n",
               iterations, total_elapsed, total_elapsed / iterations);
    }

    return 0;
}

/* ===== Statistics ===== */

int gpu_cfr_cuda_get_stats(
    const gpu_cfr_context_t* ctx_opaque,
    gpu_cfr_stats_t* stats
) {
    if (!ctx_opaque || !stats) return -1;

    const gpu_cfr_cuda_context_t* ctx = (const gpu_cfr_cuda_context_t*)ctx_opaque;
    *stats = ctx->stats;

    return 0;
}

int gpu_cfr_cuda_reset(gpu_cfr_context_t* ctx_opaque) {
    if (!ctx_opaque) return -1;

    gpu_cfr_cuda_context_t* ctx = (gpu_cfr_cuda_context_t*)ctx_opaque;

    size_t matrix_size = ctx->config.num_infosets * ctx->config.max_actions;
    size_t matrix_bytes = matrix_size * sizeof(float);

    /* Zero device matrices */
    CUDA_CHECK(cudaMemset(ctx->d_regrets, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_avg_strategy, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_curr_strategy, 0, matrix_bytes));
    CUDA_CHECK(cudaMemset(ctx->d_deltas, 0, matrix_bytes));

    /* Zero host storage */
    memset(ctx->host_storage->regrets, 0, matrix_bytes);
    memset(ctx->host_storage->avg_strategy, 0, matrix_bytes);
    memset(ctx->host_storage->curr_strategy, 0, matrix_bytes);

    /* Reset statistics */
    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    return 0;
}
