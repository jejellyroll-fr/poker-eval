/*
 * gpu_cfr.c - GPU-CFR implementation (stub/skeleton)
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Matrix-based CFR solver using GPU acceleration.
 * Full implementation requires CUDA kernels for AXPY, SpMV, and regret matching.
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* GPU-CFR context structure */
struct gpu_cfr_context_t {
    gpu_cfr_config_t config;

    /* Host storage */
    cfr_matrix_storage_t* host_storage;

    /* Device pointers (CUDA/OpenCL) */
    void* d_regrets;
    void* d_avg_strategy;
    void* d_curr_strategy;
    void* d_action_counts;

    /* Sparse transition matrix (device) */
    void* d_csr_row_ptr;
    void* d_csr_col_idx;
    void* d_csr_values;

    /* Statistics */
    gpu_cfr_stats_t stats;

    /* GPU backend context */
    void* gpu_backend;  /* gpu_eval_context_t* or similar */
};

/* ===== Configuration ===== */

gpu_cfr_config_t gpu_cfr_default_config(void) {
    gpu_cfr_config_t cfg;
    cfg.num_infosets = 10000;
    cfg.max_actions = 8;
    cfg.max_iterations = 1000;
    cfg.regret_discount = 1.0;
    cfg.strategy_weight = 1.0;
    cfg.device_id = -1;
    cfg.use_sparse = true;
    cfg.threads_per_block = 256;
    cfg.enable_profiling = false;
    cfg.verbose = false;
    return cfg;
}

/* ===== Matrix storage management ===== */

cfr_matrix_storage_t* cfr_matrix_storage_create(
    int num_infosets,
    int max_actions
) {
    cfr_matrix_storage_t* storage =
        (cfr_matrix_storage_t*)calloc(1, sizeof(cfr_matrix_storage_t));
    if (!storage) return NULL;

    storage->num_infosets = num_infosets;
    storage->max_actions = max_actions;

    size_t matrix_size = num_infosets * max_actions;

    storage->regrets = (float*)calloc(matrix_size, sizeof(float));
    storage->avg_strategy = (float*)calloc(matrix_size, sizeof(float));
    storage->curr_strategy = (float*)calloc(matrix_size, sizeof(float));
    storage->action_counts = (uint8_t*)calloc(num_infosets, sizeof(uint8_t));
    storage->infoset_keys = (uint64_t*)calloc(num_infosets, sizeof(uint64_t));

    if (!storage->regrets || !storage->avg_strategy || !storage->curr_strategy ||
        !storage->action_counts || !storage->infoset_keys) {
        cfr_matrix_storage_free(storage);
        return NULL;
    }

    return storage;
}

void cfr_matrix_storage_free(cfr_matrix_storage_t* storage) {
    if (!storage) return;

    free(storage->regrets);
    free(storage->avg_strategy);
    free(storage->curr_strategy);
    free(storage->action_counts);
    free(storage->infoset_keys);
    free(storage);
}

/* ===== Sparse matrix management ===== */

sparse_matrix_csr_t* sparse_matrix_create_csr(
    int num_infosets,
    const int* from_indices,
    const int* to_indices,
    const float* probabilities,
    int num_transitions
) {
    sparse_matrix_csr_t* matrix =
        (sparse_matrix_csr_t*)calloc(1, sizeof(sparse_matrix_csr_t));
    if (!matrix) return NULL;

    matrix->num_rows = num_infosets;
    matrix->num_cols = num_infosets;
    matrix->nnz = num_transitions;

    matrix->row_ptr = (int*)calloc(num_infosets + 1, sizeof(int));
    matrix->col_idx = (int*)calloc(num_transitions, sizeof(int));
    matrix->values = (float*)calloc(num_transitions, sizeof(float));

    if (!matrix->row_ptr || !matrix->col_idx || !matrix->values) {
        sparse_matrix_free(matrix);
        return NULL;
    }

    /* Build CSR format */
    /* Count non-zeros per row */
    for (int i = 0; i < num_transitions; i++) {
        matrix->row_ptr[from_indices[i] + 1]++;
    }

    /* Cumulative sum to get row pointers */
    for (int i = 0; i < num_infosets; i++) {
        matrix->row_ptr[i + 1] += matrix->row_ptr[i];
    }

    /* Fill column indices and values */
    int* current_pos = (int*)calloc(num_infosets, sizeof(int));
    if (!current_pos) {
        sparse_matrix_free(matrix);
        return NULL;
    }
    for (int i = 0; i < num_transitions; i++) {
        int row = from_indices[i];
        int pos = matrix->row_ptr[row] + current_pos[row];

        matrix->col_idx[pos] = to_indices[i];
        matrix->values[pos] = probabilities[i];

        current_pos[row]++;
    }

    free(current_pos);
    return matrix;
}

void sparse_matrix_free(sparse_matrix_csr_t* matrix) {
    if (!matrix) return;

    free(matrix->row_ptr);
    free(matrix->col_idx);
    free(matrix->values);
    free(matrix);
}

/* ===== GPU-CFR initialization ===== */

gpu_cfr_context_t* gpu_cfr_init(const gpu_cfr_config_t* config) {
    if (!config) return NULL;

    gpu_cfr_context_t* ctx =
        (gpu_cfr_context_t*)calloc(1, sizeof(gpu_cfr_context_t));
    if (!ctx) return NULL;

    ctx->config = *config;

    /* Allocate host storage */
    ctx->host_storage = cfr_matrix_storage_create(
        config->num_infosets,
        config->max_actions
    );

    if (!ctx->host_storage) {
        gpu_cfr_free(ctx);
        return NULL;
    }

    if (config->verbose) {
        printf("GPU-CFR initialized:\n");
        printf("  Infosets: %d\n", config->num_infosets);
        printf("  Max actions: %d\n", config->max_actions);
        printf("  Matrix size: %d × %d\n", config->num_infosets, config->max_actions);
        printf("  Memory (GPU): %.2f MB\n",
               (config->num_infosets * config->max_actions * 3 * sizeof(float)) / (1024.0 * 1024.0));
    }

    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    return ctx;
}

void gpu_cfr_free(gpu_cfr_context_t* ctx) {
    if (!ctx) return;

    cfr_matrix_storage_free(ctx->host_storage);

    free(ctx);
}

/* ===== State management ===== */

int gpu_cfr_load_state(
    gpu_cfr_context_t* ctx,
    const cfr_matrix_storage_t* storage
) {
    if (!ctx || !storage || storage->num_infosets != ctx->config.num_infosets ||
        storage->max_actions != ctx->config.max_actions)
        return -1;

    /* Copy to host storage */
    size_t matrix_size = storage->num_infosets * storage->max_actions;

    for (size_t i = 0u; i < matrix_size; ++i) {
        ctx->host_storage->regrets[i] = storage->regrets[i];
        ctx->host_storage->avg_strategy[i] = storage->avg_strategy[i];
    }
    for (size_t i = 0u; i < storage->num_infosets; ++i) {
        ctx->host_storage->action_counts[i] = storage->action_counts[i];
        ctx->host_storage->infoset_keys[i] = storage->infoset_keys[i];
    }

    return 0;
}

int gpu_cfr_download_state(
    gpu_cfr_context_t* ctx,
    cfr_matrix_storage_t* storage
) {
    if (!ctx || !storage || storage->num_infosets != ctx->config.num_infosets ||
        storage->max_actions != ctx->config.max_actions)
        return -1;

    /* Copy from host storage */
    size_t matrix_size = ctx->host_storage->num_infosets * ctx->host_storage->max_actions;

    for (size_t i = 0u; i < matrix_size; ++i) {
        storage->regrets[i] = ctx->host_storage->regrets[i];
        storage->avg_strategy[i] = ctx->host_storage->avg_strategy[i];
        storage->curr_strategy[i] = ctx->host_storage->curr_strategy[i];
    }
    for (size_t i = 0u; i < storage->num_infosets; ++i) {
        storage->action_counts[i] = ctx->host_storage->action_counts[i];
        storage->infoset_keys[i] = ctx->host_storage->infoset_keys[i];
    }

    return 0;
}

/* ===== CFR solving ===== */

int gpu_cfr_solve(
    gpu_cfr_context_t* ctx,
    int iterations
) {
    if (!ctx || iterations <= 0) return -1;

    if (ctx->config.verbose) {
        printf("Running %d GPU-CFR iterations...\n", iterations);
    }
    /* This legacy matrix API has no game-tree callback, so it cannot invent
       regret deltas. It now performs the useful, deterministic matrix part of
       CFR (regret matching and average-strategy accumulation) instead of
       silently looping as a no-op. Full tree solving belongs to pe_solver_run
       with a compute port. */
    clock_t start = clock();
    for (int iter = 0; iter < iterations; iter++) {
        if (ctx->config.verbose && (iter % 100 == 0)) {
            printf("  Iteration %d/%d\n", iter, iterations);
        }
        for (int row = 0; row < ctx->host_storage->num_infosets; ++row)
        {
            int actions = (int)ctx->host_storage->action_counts[row];
            if (actions <= 0 || actions > ctx->host_storage->max_actions)
                continue;
            int base = row * ctx->host_storage->max_actions;
            float positive = 0.0f;
            for (int action = 0; action < actions; ++action)
                if (ctx->host_storage->regrets[base + action] > 0.0f)
                    positive += ctx->host_storage->regrets[base + action];
            float uniform = 1.0f / (float)actions;
            for (int action = 0; action < actions; ++action)
            {
                float regret = ctx->host_storage->regrets[base + action];
                float strategy = positive > 0.0f
                    ? (regret > 0.0f ? regret / positive : 0.0f) : uniform;
                ctx->host_storage->curr_strategy[base + action] = strategy;
                ctx->host_storage->avg_strategy[base + action] +=
                    strategy * ctx->config.strategy_weight;
            }
        }
    }
    ctx->stats.total_time_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
    ctx->stats.avg_iteration_time_ms = ctx->stats.total_time_ms / (double)iterations;
    ctx->stats.iterations_completed += iterations;

    return 0;
}

/* ===== Statistics ===== */

int gpu_cfr_get_stats(
    const gpu_cfr_context_t* ctx,
    gpu_cfr_stats_t* stats
) {
    if (!ctx || !stats) return -1;

    *stats = ctx->stats;
    return 0;
}

int gpu_cfr_reset(gpu_cfr_context_t* ctx) {
    if (!ctx) return -1;

    size_t matrix_size = ctx->host_storage->num_infosets *
                         ctx->host_storage->max_actions;

    memset(ctx->host_storage->regrets, 0, matrix_size * sizeof(float));
    memset(ctx->host_storage->avg_strategy, 0, matrix_size * sizeof(float));
    memset(ctx->host_storage->curr_strategy, 0, matrix_size * sizeof(float));

    /* TODO: Reset GPU memory */
    /* cudaMemset(ctx->d_regrets, 0, ...) */

    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    return 0;
}
