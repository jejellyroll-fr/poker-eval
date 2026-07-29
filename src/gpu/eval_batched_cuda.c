/*
 * eval_batched_cuda.c - CUDA backend implementation for batched evaluation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Complete CUDA backend: device initialization, memory management,
 * lookup table loading, and kernel launch.
 */

#include <poker_eval/gpu/eval_batched_gpu.h>
#include "gpu_table_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_CUDA

#include <cuda_runtime.h>

/* CUDA error checking macro */
#define CUDA_CHECK(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, \
                cudaGetErrorString(err)); \
        return -1; \
    } \
} while(0)

#define CUDA_CHECK_RETURN(call, retval) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, \
                cudaGetErrorString(err)); \
        return retval; \
    } \
} while(0)

/* Forward declarations of CUDA kernels (defined in .cu file) */
extern "C" void launch_eval_holdem_batch_kernel(
    const uint8_t* d_hands,
    size_t batch_size,
    uint32_t* d_values,
    cudaStream_t stream
);

extern "C" void launch_eval_holdem_batch_kernel_opt(
    const uint8_t* d_hands,
    size_t batch_size,
    uint32_t* d_values,
    cudaStream_t stream
);

extern "C" void launch_eval_omaha_batch_kernel(
    const uint8_t* d_hands,
    size_t batch_size,
    uint32_t* d_values,
    cudaStream_t stream
);

extern "C" void launch_eval_omaha5_batch_kernel(
    const uint8_t* d_hands,
    size_t batch_size,
    uint32_t* d_values,
    cudaStream_t stream
);

extern "C" void launch_eval_omaha6_batch_kernel(
    const uint8_t* d_hands,
    size_t batch_size,
    uint32_t* d_values,
    cudaStream_t stream
);

extern "C" void launch_eval_equity_kernel(
    uint8_t hero0,
    uint8_t hero1,
    uint8_t villain0,
    uint8_t villain1,
    const uint8_t* d_boards,
    size_t num_boards,
    unsigned long long* d_wins,
    unsigned long long* d_ties,
    unsigned long long* d_losses,
    cudaStream_t stream
);

/* CUDA context structure */
typedef struct {
    int device_id;
    cudaStream_t stream;

    /* Concurrent streams for overlapping I/O and compute */
    int num_streams;
    cudaStream_t* streams;
    cudaEvent_t* stream_events;

    /* Device memory buffers */
    uint8_t* d_hands;
    uint32_t* d_values;
    size_t buffer_capacity;
    size_t cards_per_hand_capacity;

    uint8_t* d_boards;
    size_t boards_capacity;
    unsigned long long* d_wins;
    unsigned long long* d_ties;
    unsigned long long* d_losses;

    /* Per-stream buffers (currently unused placeholders) */
    uint8_t** d_hands_per_stream;
    uint32_t** d_values_per_stream;
    size_t* per_stream_capacity;

    /* Host staging buffer for packing interleaved hands (Omaha) */
    uint8_t* host_interleaved;
    size_t host_interleaved_capacity; /* in cards */

    /* Performance tracking */
    cudaEvent_t start_event;
    cudaEvent_t stop_event;
    bool profiling_enabled;
} cuda_context_t;

/* ===== Lookup Table Data (will be copied to device) ===== */

/* Real poker-eval lookup tables */
static gpu_lookup_tables_t host_tables;
static bool tables_initialized = false;

/* Initialize lookup tables from real poker-eval tables */
static void init_lookup_tables(void) {
    if (tables_initialized) return;

    /* Load real poker-eval lookup tables */
    if (gpu_load_all_tables(&host_tables) != 0) {
        fprintf(stderr, "Warning: Failed to load poker-eval tables\n");
        memset(&host_tables, 0, sizeof(host_tables));
    } else {
        /* Validate loaded tables */
        if (gpu_validate_tables(&host_tables) != 0) {
            fprintf(stderr, "Warning: Loaded tables failed validation\n");
        }
    }

    tables_initialized = true;
}

/* External symbols from kernel (constant memory) */
extern __constant__ uint8_t d_nbits_table[8192];
extern __constant__ uint8_t d_straight_table[8192];
extern __constant__ uint8_t d_top_card_table[8192];
extern __constant__ uint32_t d_top_five_table[8192];

/* ===== Internal Helpers ===== */

/* Forward declaration */
static int ensure_host_interleaved(cuda_context_t* ctx, size_t batch_size, int cards_per_hand);

static int ensure_device_buffers(cuda_context_t* ctx, size_t batch_size, int cards_per_hand) {
    size_t required_capacity = batch_size;
    if (required_capacity < ctx->buffer_capacity) {
        required_capacity = ctx->buffer_capacity;
    }

    bool need_realloc = false;
    if (ctx->d_hands == NULL || ctx->buffer_capacity < batch_size) {
        need_realloc = true;
    }
    if (cards_per_hand > ctx->cards_per_hand_capacity) {
        need_realloc = true;
    }

    if (need_realloc) {
        if (ctx->d_hands) cudaFree(ctx->d_hands);
        if (ctx->d_values) cudaFree(ctx->d_values);

        size_t hands_size = required_capacity * cards_per_hand * sizeof(uint8_t);
        size_t values_size = required_capacity * sizeof(uint32_t);

        CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_hands, hands_size), -1);
        CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_values, values_size), -1);

        ctx->buffer_capacity = required_capacity;
        ctx->cards_per_hand_capacity = cards_per_hand;
    }

    return 0;
}

int cuda_backend_eval_omaha5_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    if (!context || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    cuda_context_t* ctx = (cuda_context_t*)context;

    /* 5 hole cards + 5 board cards = 10 cards */
    if (ensure_device_buffers(ctx, batch_size, 10) != 0) {
        return -1;
    }
    if (ensure_host_interleaved(ctx, batch_size, 10) != 0) {
        return -1;
    }

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->start_event, ctx->stream);
    }

    uint8_t* host = ctx->host_interleaved;
    for (size_t i = 0; i < batch_size; i++) {
        memcpy(host + i * 10, hole + i * 5, 5);
        memcpy(host + i * 10 + 5, board + i * 5, 5);
    }

    size_t hands_size = batch_size * 10 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMemcpy(ctx->d_hands, host, hands_size, cudaMemcpyHostToDevice), -1);

    launch_eval_omaha5_batch_kernel(
        ctx->d_hands,
        batch_size,
        ctx->d_values,
        ctx->stream
    );

    size_t values_size = batch_size * sizeof(uint32_t);
    CUDA_CHECK_RETURN(cudaMemcpy(out_values, ctx->d_values, values_size, cudaMemcpyDeviceToHost), -1);

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->stop_event, ctx->stream);
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
        float elapsed_ms = 0.0f;
        cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
        *out_time_ms = (double)elapsed_ms;
    } else {
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
    }

    return 0;
}

int cuda_backend_eval_omaha6_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    if (!context || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    cuda_context_t* ctx = (cuda_context_t*)context;

    /* 6 hole cards + 5 board cards = 11 cards */
    if (ensure_device_buffers(ctx, batch_size, 11) != 0) {
        return -1;
    }
    if (ensure_host_interleaved(ctx, batch_size, 11) != 0) {
        return -1;
    }

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->start_event, ctx->stream);
    }

    uint8_t* host = ctx->host_interleaved;
    for (size_t i = 0; i < batch_size; i++) {
        memcpy(host + i * 11, hole + i * 6, 6);
        memcpy(host + i * 11 + 6, board + i * 5, 5);
    }

    size_t hands_size = batch_size * 11 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMemcpy(ctx->d_hands, host, hands_size, cudaMemcpyHostToDevice), -1);

    launch_eval_omaha6_batch_kernel(
        ctx->d_hands,
        batch_size,
        ctx->d_values,
        ctx->stream
    );

    size_t values_size = batch_size * sizeof(uint32_t);
    CUDA_CHECK_RETURN(cudaMemcpy(out_values, ctx->d_values, values_size, cudaMemcpyDeviceToHost), -1);

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->stop_event, ctx->stream);
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
        float elapsed_ms = 0.0f;
        cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
        *out_time_ms = (double)elapsed_ms;
    } else {
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
    }

    return 0;
}

static int ensure_board_buffer(cuda_context_t* ctx, size_t num_boards) {
    if (ctx->d_boards && ctx->boards_capacity >= num_boards) {
        return 0;
    }

    if (ctx->d_boards) cudaFree(ctx->d_boards);
    size_t size = num_boards * 5 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_boards, size), -1);
    ctx->boards_capacity = num_boards;
    return 0;
}

static int ensure_equity_counters(cuda_context_t* ctx) {
    if (!ctx->d_wins) {
        CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_wins, sizeof(unsigned long long)), -1);
    }
    if (!ctx->d_ties) {
        CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_ties, sizeof(unsigned long long)), -1);
    }
    if (!ctx->d_losses) {
        CUDA_CHECK_RETURN(cudaMalloc(&ctx->d_losses, sizeof(unsigned long long)), -1);
    }
    return 0;
}

static int ensure_host_interleaved(cuda_context_t* ctx, size_t batch_size, int cards_per_hand) {
    size_t required_cards = batch_size * (size_t)cards_per_hand;
    if (ctx->host_interleaved_capacity >= required_cards) {
        return 0;
    }

    uint8_t* new_buffer = (uint8_t*)realloc(ctx->host_interleaved, required_cards * sizeof(uint8_t));
    if (!new_buffer) {
        return -1;
    }
    ctx->host_interleaved = new_buffer;
    ctx->host_interleaved_capacity = required_cards;
    return 0;
}

/* ===== CUDA Backend Implementation ===== */

int cuda_backend_init(void** out_context, int device_id, bool verbose) {
    int device_count = 0;
    CUDA_CHECK_RETURN(cudaGetDeviceCount(&device_count), -1);

    if (device_count == 0) {
        if (verbose) {
            fprintf(stderr, "No CUDA devices found\n");
        }
        return -1;
    }

    /* Select device */
    int selected_device = device_id;
    if (selected_device < 0) {
        selected_device = 0; /* Default to first device */
    }

    if (selected_device >= device_count) {
        if (verbose) {
            fprintf(stderr, "Device ID %d out of range (max: %d)\n",
                    selected_device, device_count - 1);
        }
        return -1;
    }

    CUDA_CHECK_RETURN(cudaSetDevice(selected_device), -1);

    /* Allocate context */
    cuda_context_t* ctx = (cuda_context_t*)calloc(1, sizeof(cuda_context_t));
    if (!ctx) return -1;

    ctx->device_id = selected_device;
    ctx->buffer_capacity = 0;
    ctx->cards_per_hand_capacity = 0;
    ctx->d_hands = NULL;
    ctx->d_values = NULL;
    ctx->d_boards = NULL;
    ctx->boards_capacity = 0;
    ctx->d_wins = NULL;
    ctx->d_ties = NULL;
    ctx->d_losses = NULL;
    ctx->profiling_enabled = false;
    ctx->host_interleaved = NULL;
    ctx->host_interleaved_capacity = 0;

    /* Create main CUDA stream */
    CUDA_CHECK_RETURN(cudaStreamCreate(&ctx->stream), -1);

    /* Create concurrent streams for overlapping I/O and compute */
    ctx->num_streams = 4; /* Default: 4 concurrent streams */
    ctx->streams = (cudaStream_t*)calloc(ctx->num_streams, sizeof(cudaStream_t));
    ctx->stream_events = (cudaEvent_t*)calloc(ctx->num_streams, sizeof(cudaEvent_t));

    for (int i = 0; i < ctx->num_streams; i++) {
        CUDA_CHECK_RETURN(cudaStreamCreate(&ctx->streams[i]), -1);
        CUDA_CHECK_RETURN(cudaEventCreate(&ctx->stream_events[i]), -1);
    }

    /* Allocate per-stream buffers */
    ctx->d_hands_per_stream = (uint8_t**)calloc(ctx->num_streams, sizeof(uint8_t*));
    ctx->d_values_per_stream = (uint32_t**)calloc(ctx->num_streams, sizeof(uint32_t*));
    ctx->per_stream_capacity = (size_t*)calloc(ctx->num_streams, sizeof(size_t));

    /* Create events for profiling */
    CUDA_CHECK_RETURN(cudaEventCreate(&ctx->start_event), -1);
    CUDA_CHECK_RETURN(cudaEventCreate(&ctx->stop_event), -1);

    /* Initialize lookup tables */
    init_lookup_tables();

    /* Copy poker-eval lookup tables to device constant memory */
    CUDA_CHECK_RETURN(
        cudaMemcpyToSymbol(d_nbits_table, host_tables.nbits_table,
                          sizeof(host_tables.nbits_table)),
        -1
    );

    CUDA_CHECK_RETURN(
        cudaMemcpyToSymbol(d_straight_table, host_tables.straight_table,
                          sizeof(host_tables.straight_table)),
        -1
    );

    CUDA_CHECK_RETURN(
        cudaMemcpyToSymbol(d_top_card_table, host_tables.top_card_table,
                          sizeof(host_tables.top_card_table)),
        -1
    );

    CUDA_CHECK_RETURN(
        cudaMemcpyToSymbol(d_top_five_table, host_tables.top_five_table,
                          sizeof(host_tables.top_five_table)),
        -1
    );

    if (verbose) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, selected_device);
        fprintf(stderr, "CUDA device %d: %s\n", selected_device, prop.name);
        fprintf(stderr, "  Compute capability: %d.%d\n",
                prop.major, prop.minor);
        fprintf(stderr, "  Global memory: %.2f GB\n",
                prop.totalGlobalMem / (1024.0 * 1024.0 * 1024.0));
        fprintf(stderr, "  Lookup tables loaded to constant memory\n");
    }

    *out_context = ctx;
    return 0;
}

void cuda_backend_free(void* context) {
    if (!context) return;

    cuda_context_t* ctx = (cuda_context_t*)context;

    /* Free device buffers */
    if (ctx->d_hands) cudaFree(ctx->d_hands);
    if (ctx->d_values) cudaFree(ctx->d_values);
    if (ctx->d_boards) cudaFree(ctx->d_boards);
    if (ctx->d_wins) cudaFree(ctx->d_wins);
    if (ctx->d_ties) cudaFree(ctx->d_ties);
    if (ctx->d_losses) cudaFree(ctx->d_losses);

    /* Free per-stream buffers */
    if (ctx->d_hands_per_stream) {
        for (int i = 0; i < ctx->num_streams; i++) {
            if (ctx->d_hands_per_stream[i]) {
                cudaFree(ctx->d_hands_per_stream[i]);
            }
        }
        free(ctx->d_hands_per_stream);
    }

    if (ctx->d_values_per_stream) {
        for (int i = 0; i < ctx->num_streams; i++) {
            if (ctx->d_values_per_stream[i]) {
                cudaFree(ctx->d_values_per_stream[i]);
            }
        }
        free(ctx->d_values_per_stream);
    }

    if (ctx->per_stream_capacity) {
        free(ctx->per_stream_capacity);
    }

    if (ctx->host_interleaved) {
        free(ctx->host_interleaved);
    }

    /* Destroy concurrent streams and events */
    if (ctx->streams) {
        for (int i = 0; i < ctx->num_streams; i++) {
            cudaStreamDestroy(ctx->streams[i]);
        }
        free(ctx->streams);
    }

    if (ctx->stream_events) {
        for (int i = 0; i < ctx->num_streams; i++) {
            cudaEventDestroy(ctx->stream_events[i]);
        }
        free(ctx->stream_events);
    }

    /* Destroy main stream and events */
    cudaStreamDestroy(ctx->stream);
    cudaEventDestroy(ctx->start_event);
    cudaEventDestroy(ctx->stop_event);

    free(ctx);
}

int cuda_backend_get_device_info(void* context, gpu_device_info_t* info) {
    if (!context || !info) return -1;

    cuda_context_t* ctx = (cuda_context_t*)context;

    cudaDeviceProp prop;
    CUDA_CHECK_RETURN(cudaGetDeviceProperties(&prop, ctx->device_id), -1);

    info->backend = GPU_BACKEND_CUDA;
    info->device_id = ctx->device_id;
    strncpy(info->device_name, prop.name, sizeof(info->device_name) - 1);
    info->global_mem_size = prop.totalGlobalMem;
    info->shared_mem_per_block = prop.sharedMemPerBlock;
    info->max_threads_per_block = prop.maxThreadsPerBlock;
    info->warp_size = prop.warpSize;
    info->compute_capability_major = prop.major;
    info->compute_capability_minor = prop.minor;

    return 0;
}

/* Ensure device buffers are large enough */
int cuda_backend_eval_holdem_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    if (!context || !hands || !out_values || batch_size == 0) {
        return -1;
    }

    cuda_context_t* ctx = (cuda_context_t*)context;

    /* Ensure buffers are large enough */
    if (ensure_device_buffers(ctx, batch_size, 7) != 0) {
        return -1;
    }

    /* Start profiling */
    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->start_event, ctx->stream);
    }

    /* Copy input to device */
    size_t hands_size = batch_size * 7 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMemcpy(ctx->d_hands, hands, hands_size, cudaMemcpyHostToDevice), -1);

    /* Launch kernel */
    launch_eval_holdem_batch_kernel_opt(
        ctx->d_hands,
        batch_size,
        ctx->d_values,
        ctx->stream
    );

    /* Copy results back */
    size_t values_size = batch_size * sizeof(uint32_t);
    CUDA_CHECK_RETURN(cudaMemcpy(out_values, ctx->d_values, values_size, cudaMemcpyDeviceToHost), -1);

    /* Stop profiling */
    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->stop_event, ctx->stream);
    }

    /* Wait for completion */
    CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);

    /* Get elapsed time */
    if (ctx->profiling_enabled && out_time_ms) {
        float elapsed_ms = 0.0f;
        cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
        *out_time_ms = (double)elapsed_ms;
    }

    return 0;
}

int cuda_backend_eval_omaha_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    if (!context || !hole || !board || !out_values || batch_size == 0) {
        return -1;
    }

    cuda_context_t* ctx = (cuda_context_t*)context;

    if (ensure_device_buffers(ctx, batch_size, 9) != 0) {
        return -1;
    }
    if (ensure_host_interleaved(ctx, batch_size, 9) != 0) {
        return -1;
    }

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->start_event, ctx->stream);
    }

    uint8_t* host = ctx->host_interleaved;
    for (size_t i = 0; i < batch_size; i++) {
        memcpy(host + i * 9, hole + i * 4, 4);
        memcpy(host + i * 9 + 4, board + i * 5, 5);
    }

    size_t hands_size = batch_size * 9 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMemcpy(ctx->d_hands, host, hands_size, cudaMemcpyHostToDevice), -1);

    launch_eval_omaha_batch_kernel(
        ctx->d_hands,
        batch_size,
        ctx->d_values,
        ctx->stream
    );

    size_t values_size = batch_size * sizeof(uint32_t);
    CUDA_CHECK_RETURN(cudaMemcpy(out_values, ctx->d_values, values_size, cudaMemcpyDeviceToHost), -1);

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->stop_event, ctx->stream);
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
        float elapsed_ms = 0.0f;
        cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
        *out_time_ms = (double)elapsed_ms;
    } else {
        CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);
    }

    return 0;
}

int cuda_backend_eval_equity_holdem(
    void* context,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses,
    double* out_time_ms
) {
    if (!context || !hero_hand || !villain_hand || !boards ||
        !out_wins || !out_ties || !out_losses || num_boards == 0) {
        return -1;
    }

    cuda_context_t* ctx = (cuda_context_t*)context;

    if (ensure_board_buffer(ctx, num_boards) != 0) {
        return -1;
    }
    if (ensure_equity_counters(ctx) != 0) {
        return -1;
    }

    size_t boards_size = num_boards * 5 * sizeof(uint8_t);
    CUDA_CHECK_RETURN(cudaMemcpy(ctx->d_boards, boards, boards_size, cudaMemcpyHostToDevice), -1);

    CUDA_CHECK_RETURN(cudaMemset(ctx->d_wins, 0, sizeof(unsigned long long)), -1);
    CUDA_CHECK_RETURN(cudaMemset(ctx->d_ties, 0, sizeof(unsigned long long)), -1);
    CUDA_CHECK_RETURN(cudaMemset(ctx->d_losses, 0, sizeof(unsigned long long)), -1);

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->start_event, ctx->stream);
    }

    uint8_t h0 = hero_hand[0];
    uint8_t h1 = hero_hand[1];
    uint8_t v0 = villain_hand[0];
    uint8_t v1 = villain_hand[1];

    launch_eval_equity_kernel(
        h0,
        h1,
        v0,
        v1,
        ctx->d_boards,
        num_boards,
        ctx->d_wins,
        ctx->d_ties,
        ctx->d_losses,
        ctx->stream
    );

    CUDA_CHECK_RETURN(cudaStreamSynchronize(ctx->stream), -1);

    unsigned long long wins = 0, ties = 0, losses = 0;
    CUDA_CHECK_RETURN(cudaMemcpy(&wins, ctx->d_wins, sizeof(unsigned long long), cudaMemcpyDeviceToHost), -1);
    CUDA_CHECK_RETURN(cudaMemcpy(&ties, ctx->d_ties, sizeof(unsigned long long), cudaMemcpyDeviceToHost), -1);
    CUDA_CHECK_RETURN(cudaMemcpy(&losses, ctx->d_losses, sizeof(unsigned long long), cudaMemcpyDeviceToHost), -1);

    *out_wins = wins;
    *out_ties = ties;
    *out_losses = losses;

    if (ctx->profiling_enabled && out_time_ms) {
        cudaEventRecord(ctx->stop_event, ctx->stream);
        float elapsed_ms = 0.0f;
        cudaEventElapsedTime(&elapsed_ms, ctx->start_event, ctx->stop_event);
        *out_time_ms = (double)elapsed_ms;
    }

    return 0;
}

void cuda_backend_enable_profiling(void* context, bool enable) {
    if (!context) return;
    cuda_context_t* ctx = (cuda_context_t*)context;
    ctx->profiling_enabled = enable;
}

int cuda_backend_get_device_count(void) {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) {
        return 0;
    }
    return count;
}

/* Stud high evaluation reuses the Hold'em kernel (same 7-card, C(7,5)=21 eval) */
int cuda_backend_eval_stud_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    /* Stud is identical to Hold'em for high evaluation */
    return cuda_backend_eval_holdem_batch(context, hands, batch_size,
                                          out_values, out_time_ms);
}

/* Razz / HiLo not yet supported on CUDA — requires low eval kernels */
int cuda_backend_eval_razz_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context; (void)hands; (void)batch_size;
    (void)out_values; (void)out_time_ms;
    fprintf(stderr, "Razz batch not yet supported on CUDA\n");
    return -1;
}

int cuda_backend_eval_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
) {
    (void)context; (void)hands; (void)batch_size;
    (void)cards_per_hand; (void)game_type;
    (void)out_hi_values; (void)out_lo_values; (void)out_lo_qualifies;
    (void)out_time_ms;
    fprintf(stderr, "Hi/Lo batch not yet supported on CUDA\n");
    return -1;
}

int cuda_backend_eval_omaha_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
) {
    (void)context; (void)hands; (void)batch_size;
    (void)out_hi_values; (void)out_lo_values; (void)out_lo_qualifies;
    (void)out_time_ms;
    fprintf(stderr, "Omaha Hi/Lo batch not yet supported on CUDA\n");
    return -1;
}

#else /* !ENABLE_CUDA */

/* Stub implementations when CUDA is not available */

int cuda_backend_init(void** out_context, int device_id, bool verbose) {
    (void)out_context;
    (void)device_id;
    if (verbose) {
        fprintf(stderr, "CUDA support not compiled in\n");
    }
    return -1;
}

void cuda_backend_free(void* context) {
    (void)context;
}

int cuda_backend_get_device_info(void* context, gpu_device_info_t* info) {
    (void)context;
    (void)info;
    return -1;
}

int cuda_backend_eval_holdem_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hands;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_omaha_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hole;
    (void)board;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_equity_holdem(
    void* context,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses,
    double* out_time_ms
) {
    (void)context;
    (void)hero_hand;
    (void)villain_hand;
    (void)boards;
    (void)num_boards;
    (void)out_wins;
    (void)out_ties;
    (void)out_losses;
    (void)out_time_ms;
    return -1;
}

void cuda_backend_enable_profiling(void* context, bool enable) {
    (void)context;
    (void)enable;
}

int cuda_backend_get_device_count(void) {
    return 0;
}

int cuda_backend_eval_omaha5_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hole;
    (void)board;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_omaha6_batch(
    void* context,
    const uint8_t* hole,
    const uint8_t* board,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hole;
    (void)board;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_stud_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hands;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_razz_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values,
    double* out_time_ms
) {
    (void)context;
    (void)hands;
    (void)batch_size;
    (void)out_values;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
) {
    (void)context;
    (void)hands;
    (void)batch_size;
    (void)cards_per_hand;
    (void)game_type;
    (void)out_hi_values;
    (void)out_lo_values;
    (void)out_lo_qualifies;
    (void)out_time_ms;
    return -1;
}

int cuda_backend_eval_omaha_hilo_batch(
    void* context,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies,
    double* out_time_ms
) {
    (void)context;
    (void)hands;
    (void)batch_size;
    (void)out_hi_values;
    (void)out_lo_values;
    (void)out_lo_qualifies;
    (void)out_time_ms;
    return -1;
}

#endif /* ENABLE_CUDA */
