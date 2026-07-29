/*
 * eval_multi_gpu.c - Multi-GPU implementation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Distributes poker hand evaluation across multiple GPUs for maximum throughput.
 * Supports both CUDA and OpenCL backends.
 */

#include "include/eval_multi_gpu.h"
#include "include/eval_batched_gpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poker_eval/core/pthread_compat.h>

/* Multi-GPU context structure */
struct multi_gpu_context_t {
    gpu_eval_context_t** gpu_contexts;  /* Array of GPU contexts */
    int num_devices;                     /* Number of active GPUs */
    multi_gpu_config_t config;

    /* Work distribution */
    pthread_mutex_t work_mutex;
    size_t next_device;                  /* For round-robin */

    /* Statistics */
    uint64_t total_evals;
    double total_time_ms;
    uint64_t* per_gpu_evals;
    double* per_gpu_time_ms;

    /* Thread pool (for async evaluation) */
    pthread_t* worker_threads;
    bool threads_active;
};

typedef enum {
    MGPU_TASK_HOLDEM = 0,
    MGPU_TASK_OMAHA = 1
} mgpu_task_t;

/* Worker thread data */
typedef struct {
    multi_gpu_context_t* mgpu_ctx;
    int device_idx;
    const uint8_t* hands;
    size_t offset;
    size_t count;
    uint32_t* out_values;
    int result;
    pthread_mutex_t* done_mutex;
    pthread_cond_t* done_cond;
    bool done;
    mgpu_task_t task;
    int cards_per_hand;
} worker_data_t;

/* ===== Configuration ===== */

multi_gpu_config_t multi_gpu_default_config(void) {
    multi_gpu_config_t cfg;
    cfg.num_devices = -1; /* Use all available */
    cfg.device_ids = NULL;
    cfg.strategy = MULTI_GPU_STATIC_SPLIT;
    cfg.preferred_backend = GPU_BACKEND_CUDA;
    cfg.batch_size_per_gpu = 1048576; /* 1M hands per GPU */
    cfg.enable_profiling = false;
    cfg.verbose = false;
    return cfg;
}

/* ===== Initialization ===== */

int multi_gpu_is_available(void) {
    /* Query number of GPUs */
    int cuda_count = gpu_eval_get_device_count(GPU_BACKEND_CUDA);
    int opencl_count = gpu_eval_get_device_count(GPU_BACKEND_OPENCL);
    return cuda_count > opencl_count ? cuda_count : opencl_count;
}

multi_gpu_context_t* multi_gpu_init(const multi_gpu_config_t* config) {
    multi_gpu_context_t* ctx = (multi_gpu_context_t*)calloc(1, sizeof(multi_gpu_context_t));
    if (!ctx) return NULL;

    /* Use default config if not provided */
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config = multi_gpu_default_config();
    }

    /* Determine number of devices to use */
    int available_devices = multi_gpu_is_available();
    if (available_devices < 2) {
        if (ctx->config.verbose) {
            fprintf(stderr, "Multi-GPU requires at least 2 GPUs, found %d\n", available_devices);
        }
        free(ctx);
        return NULL;
    }

    if (ctx->config.num_devices < 0 || ctx->config.num_devices > available_devices) {
        ctx->num_devices = available_devices;
    } else {
        ctx->num_devices = ctx->config.num_devices;
    }

    if (ctx->config.verbose) {
        printf("Initializing multi-GPU with %d devices\n", ctx->num_devices);
    }

    /* Allocate GPU contexts */
    ctx->gpu_contexts = (gpu_eval_context_t**)calloc(ctx->num_devices, sizeof(gpu_eval_context_t*));
    ctx->per_gpu_evals = (uint64_t*)calloc(ctx->num_devices, sizeof(uint64_t));
    ctx->per_gpu_time_ms = (double*)calloc(ctx->num_devices, sizeof(double));

    if (!ctx->gpu_contexts || !ctx->per_gpu_evals || !ctx->per_gpu_time_ms) {
        multi_gpu_free(ctx);
        return NULL;
    }

    /* Initialize each GPU */
    for (int i = 0; i < ctx->num_devices; i++) {
        gpu_eval_config_t gpu_cfg;
        gpu_cfg.preferred_backend = ctx->config.preferred_backend;
        gpu_cfg.device_id = ctx->config.device_ids ? ctx->config.device_ids[i] : i;
        gpu_cfg.max_batch_size = ctx->config.batch_size_per_gpu;
        gpu_cfg.enable_profiling = ctx->config.enable_profiling;
        gpu_cfg.verbose = ctx->config.verbose;

        ctx->gpu_contexts[i] = gpu_eval_init(&gpu_cfg);
        if (!ctx->gpu_contexts[i]) {
            if (ctx->config.verbose) {
                fprintf(stderr, "Failed to initialize GPU %d\n", i);
            }
            multi_gpu_free(ctx);
            return NULL;
        }

        if (ctx->config.verbose) {
            gpu_device_info_t info;
            gpu_eval_get_device_info(ctx->gpu_contexts[i], &info);
            printf("  GPU %d: %s\n", i, info.name);
        }
    }

    /* Initialize mutex for work distribution */
    pthread_mutex_init(&ctx->work_mutex, NULL);
    ctx->next_device = 0;

    ctx->total_evals = 0;
    ctx->total_time_ms = 0.0;

    return ctx;
}

void multi_gpu_free(multi_gpu_context_t* ctx) {
    if (!ctx) return;

    if (ctx->gpu_contexts) {
        for (int i = 0; i < ctx->num_devices; i++) {
            if (ctx->gpu_contexts[i]) {
                gpu_eval_free(ctx->gpu_contexts[i]);
            }
        }
        free(ctx->gpu_contexts);
    }

    if (ctx->per_gpu_evals) free(ctx->per_gpu_evals);
    if (ctx->per_gpu_time_ms) free(ctx->per_gpu_time_ms);

    pthread_mutex_destroy(&ctx->work_mutex);

    free(ctx);
}

/* ===== Worker thread function ===== */

static void* gpu_worker_thread(void* arg) {
    worker_data_t* data = (worker_data_t*)arg;

    if (data->count == 0) {
        data->result = 0;
    } else if (data->task == MGPU_TASK_HOLDEM) {
        data->result = gpu_eval_holdem_batch(
            data->mgpu_ctx->gpu_contexts[data->device_idx],
            data->hands + data->offset * data->cards_per_hand,
            data->count,
            data->out_values + data->offset
        );
    } else {
        const uint8_t* base = data->hands + data->offset * data->cards_per_hand;
        size_t count = data->count;
        uint8_t* hole = (uint8_t*)malloc(count * 4 * sizeof(uint8_t));
        uint8_t* board = (uint8_t*)malloc(count * 5 * sizeof(uint8_t));
        if (!hole || !board) {
            if (hole) free(hole);
            if (board) free(board);
            data->result = -1;
        } else {
            for (size_t i = 0; i < count; ++i) {
                memcpy(hole + i * 4, base + i * data->cards_per_hand, 4);
                memcpy(board + i * 5, base + i * data->cards_per_hand + 4, 5);
            }
            data->result = gpu_eval_omaha_batch(
                data->mgpu_ctx->gpu_contexts[data->device_idx],
                hole,
                board,
                count,
                data->out_values + data->offset
            );
            free(hole);
            free(board);
        }
    }

    /* Signal completion */
    pthread_mutex_lock(data->done_mutex);
    data->done = true;
    pthread_cond_signal(data->done_cond);
    pthread_mutex_unlock(data->done_mutex);

    return NULL;
}

/* ===== Evaluation (Static Split Strategy) ===== */

int multi_gpu_eval_holdem_batch(
    multi_gpu_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !hands || !out_values || batch_size == 0) return -1;

    /* Split batch across GPUs */
    size_t hands_per_gpu = (batch_size + ctx->num_devices - 1) / ctx->num_devices;

    /* Create worker threads */
    worker_data_t* workers = (worker_data_t*)calloc(ctx->num_devices, sizeof(worker_data_t));
    pthread_t* threads = (pthread_t*)calloc(ctx->num_devices, sizeof(pthread_t));
    pthread_mutex_t done_mutex;
    pthread_cond_t done_cond;

    pthread_mutex_init(&done_mutex, NULL);
    pthread_cond_init(&done_cond, NULL);

    /* Launch workers */
    for (int i = 0; i < ctx->num_devices; i++) {
        size_t offset = i * hands_per_gpu;
        size_t count = hands_per_gpu;

        /* Last GPU handles remainder */
        if (offset + count > batch_size) {
            count = batch_size - offset;
        }

        if (count == 0) break; /* No more work */

        workers[i].mgpu_ctx = ctx;
        workers[i].device_idx = i;
        workers[i].hands = hands;
        workers[i].offset = offset;
        workers[i].count = count;
        workers[i].out_values = out_values;
        workers[i].result = 0;
        workers[i].done_mutex = &done_mutex;
        workers[i].done_cond = &done_cond;
        workers[i].done = false;
        workers[i].task = MGPU_TASK_HOLDEM;
        workers[i].cards_per_hand = 7;

        pthread_create(&threads[i], NULL, gpu_worker_thread, &workers[i]);
    }

    /* Wait for all workers */
    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0) {
            pthread_join(threads[i], NULL);
        }
    }

    /* Check results */
    int overall_result = 0;
    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0 && workers[i].result != 0) {
            overall_result = -1;
        }
    }

    /* Update statistics */
    ctx->total_evals += batch_size;
    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0) {
            ctx->per_gpu_evals[i] += workers[i].count;
        }
    }

    pthread_mutex_destroy(&done_mutex);
    pthread_cond_destroy(&done_cond);
    free(workers);
    free(threads);

    return overall_result;
}

int multi_gpu_eval_omaha_batch(
    multi_gpu_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !hands || !out_values || batch_size == 0) return -1;

    size_t hands_per_gpu = (batch_size + ctx->num_devices - 1) / ctx->num_devices;

    worker_data_t* workers = (worker_data_t*)calloc(ctx->num_devices, sizeof(worker_data_t));
    pthread_t* threads = (pthread_t*)calloc(ctx->num_devices, sizeof(pthread_t));
    pthread_mutex_t done_mutex;
    pthread_cond_t done_cond;

    pthread_mutex_init(&done_mutex, NULL);
    pthread_cond_init(&done_cond, NULL);

    for (int i = 0; i < ctx->num_devices; i++) {
        size_t offset = i * hands_per_gpu;
        size_t count = hands_per_gpu;
        if (offset + count > batch_size) {
            count = batch_size - offset;
        }
        if (count == 0) {
            workers[i].count = 0;
            continue;
        }

        workers[i].mgpu_ctx = ctx;
        workers[i].device_idx = i;
        workers[i].hands = hands;
        workers[i].offset = offset;
        workers[i].count = count;
        workers[i].out_values = out_values;
        workers[i].result = 0;
        workers[i].done_mutex = &done_mutex;
        workers[i].done_cond = &done_cond;
        workers[i].done = false;
        workers[i].task = MGPU_TASK_OMAHA;
        workers[i].cards_per_hand = 9;

        pthread_create(&threads[i], NULL, gpu_worker_thread, &workers[i]);
    }

    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0) {
            pthread_join(threads[i], NULL);
        }
    }

    int overall_result = 0;
    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0 && workers[i].result != 0) {
            overall_result = -1;
        }
    }

    ctx->total_evals += batch_size;
    for (int i = 0; i < ctx->num_devices; i++) {
        if (workers[i].count > 0) {
            ctx->per_gpu_evals[i] += workers[i].count;
        }
    }

    pthread_mutex_destroy(&done_mutex);
    pthread_cond_destroy(&done_cond);
    free(workers);
    free(threads);

    return overall_result;
}

/* ===== Statistics ===== */

int multi_gpu_get_stats(
    const multi_gpu_context_t* ctx,
    multi_gpu_stats_t* stats
) {
    if (!ctx || !stats) return -1;

    stats->total_evals = ctx->total_evals;
    stats->total_time_ms = ctx->total_time_ms;
    stats->per_gpu_evals = ctx->per_gpu_evals;
    stats->per_gpu_time_ms = ctx->per_gpu_time_ms;

    if (ctx->total_time_ms > 0.0) {
        stats->aggregate_throughput = (double)ctx->total_evals / (ctx->total_time_ms / 1000.0);
    } else {
        stats->aggregate_throughput = 0.0;
    }

    return 0;
}

void multi_gpu_reset_stats(multi_gpu_context_t* ctx) {
    if (!ctx) return;

    ctx->total_evals = 0;
    ctx->total_time_ms = 0.0;

    for (int i = 0; i < ctx->num_devices; i++) {
        ctx->per_gpu_evals[i] = 0;
        ctx->per_gpu_time_ms[i] = 0.0;
        gpu_eval_reset_stats(ctx->gpu_contexts[i]);
    }
}

int multi_gpu_get_device_count(const multi_gpu_context_t* ctx) {
    return ctx ? ctx->num_devices : 0;
}

int multi_gpu_get_device_info(
    const multi_gpu_context_t* ctx,
    int device_idx,
    gpu_device_info_t* info
) {
    if (!ctx || device_idx < 0 || device_idx >= ctx->num_devices || !info) {
        return -1;
    }

    return gpu_eval_get_device_info(ctx->gpu_contexts[device_idx], info);
}
