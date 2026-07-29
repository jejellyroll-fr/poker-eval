/*
 * eval_batched_gpu.c - GPU batched evaluation unified interface
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Unified wrapper that dispatches to CUDA or OpenCL backends.
 */

#include <poker_eval/gpu/eval_batched_gpu.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations for CUDA backend functions */
#ifdef ENABLE_CUDA
extern int cuda_backend_init(void** out_context, int device_id, bool verbose);
extern void cuda_backend_free(void* context);
extern int cuda_backend_get_device_info(void* context, gpu_device_info_t* info);
extern int cuda_backend_eval_holdem_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha5_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha6_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_stud_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_razz_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
);
extern int cuda_backend_eval_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
);
extern int cuda_backend_eval_omaha_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
);
extern int cuda_backend_eval_equity_holdem(
    void* context,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses,
    double* out_time_ms
);
extern void cuda_backend_enable_profiling(void* context, bool enable);
extern int cuda_backend_get_device_count(void);
#endif

/* Forward declarations for OpenCL backend functions */
#ifdef ENABLE_OPENCL
#include "eval_batched_opencl.h"
#endif

/* ===== Context structure ===== */

struct gpu_eval_context_t {
    gpu_eval_config_t config;
    gpu_device_info_t device_info;

    void* backend_context;

    uint64_t total_evals;
    double total_time_ms;

    bool initialized;
};

/* ===== Configuration ===== */

gpu_eval_config_t gpu_eval_default_config(void) {
    gpu_eval_config_t cfg;
    cfg.preferred_backend = GPU_BACKEND_CUDA;
    cfg.device_id = -1;
    cfg.max_batch_size = 1048576;
    cfg.enable_profiling = false;
    cfg.verbose = false;
    return cfg;
}

/* ===== Initialization ===== */

gpu_eval_context_t* gpu_eval_init(const gpu_eval_config_t* config) {
    gpu_eval_context_t* ctx = (gpu_eval_context_t*)calloc(1, sizeof(gpu_eval_context_t));
    if (!ctx) return NULL;

    if (config) {
        ctx->config = *config;
    } else {
        ctx->config = gpu_eval_default_config();
    }

    if (ctx->config.device_id < 0) {
        const char* env = getenv("POKER_EVAL_GPU_DEVICE");
        if (env && *env) {
            char* endptr = NULL;
            long val = strtol(env, &endptr, 10);
            if (endptr != env && val >= 0 && val <= INT_MAX) {
                ctx->config.device_id = (int)val;
            }
        }
    }

    /* Try CUDA first */
#ifdef ENABLE_CUDA
    if (ctx->config.preferred_backend == GPU_BACKEND_CUDA ||
        ctx->config.preferred_backend == GPU_BACKEND_NONE) {

        if (cuda_backend_init(&ctx->backend_context,
                             ctx->config.device_id,
                             ctx->config.verbose) == 0) {
            ctx->device_info.backend = GPU_BACKEND_CUDA;
            ctx->initialized = true;
            cuda_backend_enable_profiling(ctx->backend_context,
                                         ctx->config.enable_profiling);
            cuda_backend_get_device_info(ctx->backend_context, &ctx->device_info);
            if (ctx->device_info.device_id < 0) {
                ctx->device_info.device_id = ctx->config.device_id;
            }

            ctx->total_evals = 0;
            ctx->total_time_ms = 0.0;

            return ctx;
        }
    }
#endif

    /* Try OpenCL if CUDA failed or not preferred */
#ifdef ENABLE_OPENCL
    if (ctx->config.preferred_backend == GPU_BACKEND_OPENCL ||
        ctx->config.preferred_backend == GPU_BACKEND_NONE) {

        gpu_eval_context_t* opencl_ctx = gpu_eval_init_opencl(&ctx->config);
        if (opencl_ctx) {
            ctx->backend_context = opencl_ctx;
            gpu_eval_fill_device_info_opencl(opencl_ctx, &ctx->device_info);
            ctx->initialized = true;

            ctx->total_evals = 0;
            ctx->total_time_ms = 0.0;
            if (ctx->config.device_id < 0) {
                ctx->config.device_id = ctx->device_info.device_id;
            }

            return ctx;
        }
    }
#endif

    if (ctx->config.verbose) {
        fprintf(stderr, "No GPU backend available\n");
    }

    free(ctx);
    return NULL;
}

void gpu_eval_free(gpu_eval_context_t* ctx) {
    if (!ctx) return;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        cuda_backend_free(ctx->backend_context);
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        gpu_eval_free_opencl(ctx->backend_context);
    }
#endif

    free(ctx);
}

/* ===== Device Info ===== */

int gpu_eval_get_device_info(const gpu_eval_context_t* ctx, gpu_device_info_t* info) {
    if (!ctx || !info) return -1;
    *info = ctx->device_info;
    return 0;
}

/* ===== Evaluation ===== */

int gpu_eval_holdem_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !hands || !out_values || batch_size == 0) return -1;
    if (!ctx->initialized) return -1;

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        result = cuda_backend_eval_holdem_batch(
            ctx->backend_context,
            hands,
            batch_size,
            out_values,
            ctx->config.enable_profiling ? &time_ms : NULL
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_holdem_batch_opencl(
            ctx->backend_context,
            hands,
            batch_size,
            out_values
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) {
            ctx->total_time_ms += time_ms;
        }
    }

    return result;
}

int gpu_eval_omaha5_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !ctx->initialized || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        double* time_ptr = ctx->config.enable_profiling ? &time_ms : NULL;
        result = cuda_backend_eval_omaha5_batch(
            ctx->backend_context,
            hole,
            board,
            batch_size,
            out_values,
            time_ptr
        );
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) {
            ctx->total_time_ms += time_ms;
        }
    }

    return result;
}

int gpu_eval_omaha6_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !ctx->initialized || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        double* time_ptr = ctx->config.enable_profiling ? &time_ms : NULL;
        result = cuda_backend_eval_omaha6_batch(
            ctx->backend_context,
            hole,
            board,
            batch_size,
            out_values,
            time_ptr
        );
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) {
            ctx->total_time_ms += time_ms;
        }
    }

    return result;
}

int gpu_eval_omaha_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !ctx->initialized || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        double* time_ptr = ctx->config.enable_profiling ? &time_ms : NULL;
        result = cuda_backend_eval_omaha_batch(
            ctx->backend_context,
            hole,
            board,
            batch_size,
            out_values,
            time_ptr
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        size_t total_cards = batch_size * 9;
        uint8_t* interleaved = (uint8_t*)malloc(total_cards * sizeof(uint8_t));
        if (!interleaved) {
            return -1;
        }
        for (size_t i = 0; i < batch_size; ++i) {
            memcpy(interleaved + i * 9, hole + i * 4, 4);
            memcpy(interleaved + i * 9 + 4, board + i * 5, 5);
        }
        result = gpu_eval_omaha_batch_opencl(
            ctx->backend_context,
            interleaved,
            batch_size,
            out_values
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
        free(interleaved);
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) {
            ctx->total_time_ms += time_ms;
        }
    }

    return result;
}

int gpu_eval_equity_holdem(
    gpu_eval_context_t* ctx,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses
) {
    if (!ctx || !ctx->initialized || !hero_hand || !villain_hand || !boards ||
        !out_wins || !out_ties || !out_losses || num_boards == 0) {
        return -1;
    }

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        double* time_ptr = ctx->config.enable_profiling ? &time_ms : NULL;
        result = cuda_backend_eval_equity_holdem(
            ctx->backend_context,
            hero_hand,
            villain_hand,
            boards,
            num_boards,
            out_wins,
            out_ties,
            out_losses,
            time_ptr
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_equity_holdem_opencl(
            ctx->backend_context,
            hero_hand,
            villain_hand,
            boards,
            num_boards,
            out_wins,
            out_ties,
            out_losses
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += num_boards;
        if (ctx->config.enable_profiling) {
            ctx->total_time_ms += time_ms;
        }
    }

    return result;
}

/* ===== Stud / Razz / Hi-Lo batch routing ===== */

int gpu_eval_stud_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !hands || !out_values || batch_size == 0) return -1;
    if (!ctx->initialized) return -1;

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        result = cuda_backend_eval_stud_batch(
            ctx->backend_context, hands, batch_size, out_values,
            ctx->config.enable_profiling ? &time_ms : NULL
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_stud_batch_opencl(
            ctx->backend_context, hands, batch_size, out_values
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) ctx->total_time_ms += time_ms;
    }
    return result;
}

int gpu_eval_razz_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    if (!ctx || !hands || !out_values || batch_size == 0) return -1;
    if (!ctx->initialized) return -1;

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        result = cuda_backend_eval_razz_batch(
            ctx->backend_context, hands, batch_size, out_values,
            ctx->config.enable_profiling ? &time_ms : NULL
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_razz_batch_opencl(
            ctx->backend_context, hands, batch_size, out_values
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) ctx->total_time_ms += time_ms;
    }
    return result;
}

int gpu_eval_hilo_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
) {
    if (!ctx || !hands || !out_hi_values || !out_lo_values ||
        !out_lo_qualifies || batch_size == 0) return -1;
    if (!ctx->initialized) return -1;

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        result = cuda_backend_eval_hilo_batch(
            ctx->backend_context, hands, batch_size,
            cards_per_hand, game_type,
            out_hi_values, out_lo_values, out_lo_qualifies,
            ctx->config.enable_profiling ? &time_ms : NULL
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_hilo_batch_opencl(
            ctx->backend_context, hands, batch_size,
            cards_per_hand, game_type,
            out_hi_values, out_lo_values, out_lo_qualifies
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) ctx->total_time_ms += time_ms;
    }
    return result;
}

int gpu_eval_omaha_hilo_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
) {
    if (!ctx || !hands || !out_hi_values || !out_lo_values ||
        !out_lo_qualifies || batch_size == 0) return -1;
    if (!ctx->initialized) return -1;

    double time_ms = 0.0;
    int result = -1;

#ifdef ENABLE_CUDA
    if (ctx->device_info.backend == GPU_BACKEND_CUDA) {
        result = cuda_backend_eval_omaha_hilo_batch(
            ctx->backend_context, hands, batch_size,
            out_hi_values, out_lo_values, out_lo_qualifies,
            ctx->config.enable_profiling ? &time_ms : NULL
        );
    }
#endif

#ifdef ENABLE_OPENCL
    if (ctx->device_info.backend == GPU_BACKEND_OPENCL) {
        result = gpu_eval_omaha_hilo_batch_opencl(
            ctx->backend_context, hands, batch_size,
            out_hi_values, out_lo_values, out_lo_qualifies
        );
        if (ctx->config.enable_profiling) {
            time_ms = gpu_eval_get_last_time_opencl(ctx->backend_context);
        }
    }
#endif

    if (result == 0) {
        ctx->total_evals += batch_size;
        if (ctx->config.enable_profiling) ctx->total_time_ms += time_ms;
    }
    return result;
}

/* ===== Statistics ===== */

void gpu_eval_get_stats(
    const gpu_eval_context_t* ctx,
    uint64_t* out_total_evals,
    double* out_total_time_ms,
    double* out_evals_per_sec
) {
    if (!ctx) return;

    if (out_total_evals) *out_total_evals = ctx->total_evals;
    if (out_total_time_ms) *out_total_time_ms = ctx->total_time_ms;

    if (out_evals_per_sec) {
        if (ctx->total_time_ms > 0.0) {
            *out_evals_per_sec = (double)ctx->total_evals / (ctx->total_time_ms / 1000.0);
        } else {
            *out_evals_per_sec = 0.0;
        }
    }
}

void gpu_eval_reset_stats(gpu_eval_context_t* ctx) {
    if (!ctx) return;
    ctx->total_evals = 0;
    ctx->total_time_ms = 0.0;
}

/* ===== Helpers ===== */

bool gpu_eval_is_available(void) {
    if (gpu_eval_get_device_count(GPU_BACKEND_CUDA) > 0) {
        return true;
    }
    if (gpu_eval_get_device_count(GPU_BACKEND_OPENCL) > 0) {
        return true;
    }
    return false;
}

int gpu_eval_get_device_count(gpu_backend_t backend) {
    switch (backend) {
    case GPU_BACKEND_CUDA: {
#ifdef ENABLE_CUDA
        return cuda_backend_get_device_count();
#else
        return 0;
#endif
    }
    case GPU_BACKEND_OPENCL: {
#ifdef ENABLE_OPENCL
        return gpu_eval_get_device_count_opencl();
#else
        return 0;
#endif
    }
    case GPU_BACKEND_NONE:
    default: {
        int total = 0;
#ifdef ENABLE_CUDA
        total += cuda_backend_get_device_count();
#endif
#ifdef ENABLE_OPENCL
        total += gpu_eval_get_device_count_opencl();
#endif
        return total;
    }
    }
}

size_t gpu_eval_get_recommended_batch_size(const gpu_eval_context_t* ctx) {
    if (!ctx) return 0;
    return ctx->config.max_batch_size;
}
