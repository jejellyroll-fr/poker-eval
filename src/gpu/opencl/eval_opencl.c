/*
 * eval_opencl.c: OpenCL implementation of GPU-accelerated poker evaluator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include <poker_eval/gpu/eval_gpu.h>
#include "eval_opencl.h"
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>

#define KERNEL_FILE "eval_kernel.cl"
#define MAX_DEVICES 8
#define MAX_PLATFORMS 4
#define MAX_SOURCE_SIZE (1 << 20)

/* OpenCL-specific context structure */
typedef struct
{
    gpu_eval_context_t base;
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel eval_kernel;
    cl_mem d_boards;
    cl_mem d_hole_cards;
    cl_mem d_results;
    cl_mem d_nBitsTable;
    cl_mem d_straightTable;
    cl_mem d_topFiveCardsTable;
    cl_mem d_topCardTable;
    size_t boards_capacity;
    size_t hole_cards_capacity;
    size_t results_capacity;
} opencl_eval_context_t;

/* GPU card mask structure (matches OpenCL kernel) */
typedef struct
{
    uint32_t cards_n;
    uint32_t spades;
    uint32_t clubs;
    uint32_t diamonds;
    uint32_t hearts;
} gpu_card_mask_t;

/* Convert StdDeck_CardMask to GPU format */
static void convert_to_gpu_mask(const StdDeck_CardMask *std_mask, gpu_card_mask_t *gpu_mask)
{
    gpu_mask->cards_n = 0; /* Will be set by kernel */
    gpu_mask->spades = StdDeck_CardMask_SPADES(*std_mask);
    gpu_mask->clubs = StdDeck_CardMask_CLUBS(*std_mask);
    gpu_mask->diamonds = StdDeck_CardMask_DIAMONDS(*std_mask);
    gpu_mask->hearts = StdDeck_CardMask_HEARTS(*std_mask);
}

static int opencl_debug_enabled(void)
{
    static int initialized = 0;
    static int enabled = 0;
    if (!initialized) {
        const char* env = getenv("POKER_OPENCL_DEBUG");
        if (!env) {
            enabled = 1; /* Default to verbose logging to help diagnose kernel issues */
        } else if (*env && (*env != '0')) {
            enabled = 1;
        } else {
            enabled = 0;
        }
        initialized = 1;
    }
    return enabled;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
static void opencl_debug_log(const char* fmt, ...)
{
    if (!opencl_debug_enabled())
        return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[OpenCL] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(CL_VERSION_2_0)
static cl_command_queue opencl_create_command_queue(cl_context context, cl_device_id device, cl_int* err) {
    return clCreateCommandQueueWithProperties(context, device, NULL, err);
}
#else
static cl_command_queue opencl_create_command_queue(cl_context context, cl_device_id device, cl_int* err) {
    return clCreateCommandQueue(context, device, 0, err);
}
#endif

/* Helper to load kernel source */
static char *load_kernel_source(const char *filename)
{
    /* Build list of search paths */
    char path1[512], path2[512], path3[512], path4[512], path5[512], path6[512], path7[512], path8[512];

    /* Try multiple paths for the kernel file */
    const char *search_paths[] = {
        filename,                                                       /* Direct path */
        (snprintf(path1, sizeof(path1), "../gpu/%s", filename), path1),     /* From tests/ directory */
        (snprintf(path2, sizeof(path2), "../../gpu/%s", filename), path2),  /* From build/tests/ directory */
        (snprintf(path3, sizeof(path3), "../src/gpu/opencl/%s", filename), path3), /* Preferred new location */
        (snprintf(path4, sizeof(path4), "../src/gpu/%s", filename), path4),        /* Legacy location */
        (snprintf(path5, sizeof(path5), "gpu/%s", filename), path5),              /* From project root */
        (snprintf(path6, sizeof(path6), "src/gpu/opencl/%s", filename), path6),   /* Build tree copy */
        (snprintf(path7, sizeof(path7), "src/gpu/%s", filename), path7),          /* Build tree legacy copy */
        (snprintf(path8, sizeof(path8), "opencl/%s", filename), path8),          /* Direct opencl subdir */
        NULL
    };

    FILE *f = NULL;
    const char* selected_path = NULL;
    for (int i = 0; search_paths[i] != NULL; i++) {
        f = fopen(search_paths[i], "rb");
        if (f) {
            selected_path = search_paths[i];
            break;
        }
    }

    if (!f) {
        opencl_debug_log("Failed to locate kernel source '%s'", filename);
        return NULL;
    }
    opencl_debug_log("Loaded kernel '%s' from %s", filename, selected_path);

    char *src = (char *)malloc(MAX_SOURCE_SIZE);
    size_t len = fread(src, 1, MAX_SOURCE_SIZE, f);
    src[len] = '\0';
    fclose(f);
    return src;
}

/* Pretty-print OpenCL build logs (handles UTF-16 output on macOS) */
static void print_opencl_build_log(cl_program program, cl_device_id device, cl_int build_err)
{
    size_t log_size = 0;
    cl_int err = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                                       0, NULL, &log_size);
    if (err != CL_SUCCESS || log_size == 0) {
        fprintf(stderr,
                "OpenCL build error (code %d): unable to retrieve build log (err=%d, size=%zu)\n",
                build_err, err, (size_t)log_size);
        return;
    }

    unsigned char *raw_log = (unsigned char *)malloc(log_size);
    if (!raw_log) {
        fprintf(stderr,
                "OpenCL build error (code %d): could not allocate %zu bytes for build log\n",
                build_err, (size_t)log_size);
        return;
    }

    err = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                                log_size, raw_log, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr,
                "OpenCL build error (code %d): failed to read build log (err=%d)\n",
                build_err, err);
        free(raw_log);
        return;
    }

    int printed_decoded = 0;
    if ((log_size % 2) == 0) {
        size_t half = log_size / 2;
        size_t zero_even = 0, zero_odd = 0;
        for (size_t i = 0; i < log_size; ++i) {
            if (raw_log[i] == 0) {
                if ((i & 1U) == 0)
                    zero_even++;
                else
                    zero_odd++;
            }
        }
        double even_ratio = half ? (double)zero_even / (double)half : 0.0;
        double odd_ratio = half ? (double)zero_odd / (double)half : 0.0;
        if (even_ratio > 0.6 || odd_ratio > 0.6) {
            int big_endian = (odd_ratio > even_ratio);
            char *decoded = (char *)malloc(half + 1);
            if (decoded) {
                for (size_t i = 0; i < half; ++i) {
                    unsigned char b0 = raw_log[2 * i];
                    unsigned char b1 = raw_log[2 * i + 1];
                    unsigned char ch = big_endian ? b1 : b0;
                    if (ch == 0)
                        ch = ' ';
                    decoded[i] = (char)ch;
                }
                decoded[half] = '\0';
                fprintf(stderr, "OpenCL build error (code %d):\n%s\n",
                        build_err, decoded);
                free(decoded);
                printed_decoded = 1;
            }
        }
    }

    if (!printed_decoded) {
        char *sanitized = (char *)malloc(log_size + 1);
        if (sanitized) {
            for (size_t i = 0; i < log_size; ++i) {
                unsigned char c = raw_log[i];
                if (c == '\n' || c == '\r' || c == '\t')
                    sanitized[i] = (char)c;
                else if (c >= 32 && c < 127)
                    sanitized[i] = (char)c;
                else
                    sanitized[i] = '.';
            }
            sanitized[log_size] = '\0';
            fprintf(stderr, "OpenCL build error (code %d):\n%s\n",
                    build_err, sanitized);
            free(sanitized);
        } else {
            fprintf(stderr,
                    "OpenCL build error (code %d): build log contains %zu bytes (unable to print)\n",
                    build_err, (size_t)log_size);
        }
    }

    fprintf(stderr, "Raw OpenCL build log (%zu bytes):", (size_t)log_size);
    for (size_t i = 0; i < log_size; ++i) {
        if ((i % 32) == 0)
            fprintf(stderr, "\n  ");
        fprintf(stderr, "%02x ", raw_log[i]);
    }
    fprintf(stderr, "\n");

    free(raw_log);
}

/* Initialize OpenCL context */
gpu_eval_context_t *opencl_gpu_eval_init(int device_id, size_t max_batch_size, int backend_type)
{
    if (backend_type != 1)
    { /* Only handle OpenCL in this implementation */
        return NULL;
    }
    cl_int err;
    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint num_platforms;
    cl_device_id devices[MAX_DEVICES];
    cl_uint num_devices;

    err = clGetPlatformIDs(MAX_PLATFORMS, platforms, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0)
        return NULL;

    err = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0)
        return NULL;

    if (device_id >= num_devices)
        return NULL;

    opencl_eval_context_t *ctx = (opencl_eval_context_t *)calloc(1, sizeof(opencl_eval_context_t));
    if (!ctx)
        return NULL;

    ctx->base.device_id = device_id;
    ctx->base.max_batch_size = max_batch_size;
    ctx->base.backend_type = 1; /* OpenCL */
    ctx->platform = platforms[0];
    ctx->device = devices[device_id];

    ctx->context = clCreateContext(NULL, 1, &ctx->device, NULL, NULL, &err);
    if (err != CL_SUCCESS)
    {
        free(ctx);
        return NULL;
    }

/* Use deprecated but widely supported OpenCL 1.2 API for macOS compatibility */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    ctx->queue = opencl_create_command_queue(ctx->context, ctx->device, &err);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if (err != CL_SUCCESS)
    {
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Load and build kernel */
    char *kernel_src = load_kernel_source(KERNEL_FILE);
    if (!kernel_src)
    {
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }
    const char *const_kernel_src = kernel_src;
    ctx->program = clCreateProgramWithSource(ctx->context, 1, &const_kernel_src, NULL, &err);
    free(kernel_src);
    if (err != CL_SUCCESS)
    {
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Build program with fast optimizations (relaxed math maintains correctness for poker eval) */
    const char* build_opts = "-cl-mad-enable -cl-fast-relaxed-math";
    err = clBuildProgram(ctx->program, 1, &ctx->device, build_opts, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        print_opencl_build_log(ctx->program, ctx->device, err);
        clReleaseProgram(ctx->program);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->eval_kernel = clCreateKernel(ctx->program, "eval_batch_boards_kernel", &err);
    if (err != CL_SUCCESS)
    {
        clReleaseProgram(ctx->program);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Allocate device memory for lookup tables (copied once) */
    ctx->d_nBitsTable = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 8192 * sizeof(uint8_t), nBitsTable, &err);
    ctx->d_straightTable = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 8192 * sizeof(uint8_t), straightTable, &err);
    ctx->d_topFiveCardsTable = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 8192 * sizeof(uint32_t), topFiveCardsTable, &err);
    ctx->d_topCardTable = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 8192 * sizeof(uint8_t), topCardTable, &err);

    /* Pre-allocate device memory for maximum batch size */
    size_t board_size = max_batch_size * sizeof(gpu_card_mask_t);
    size_t hole_cards_size = max_batch_size * 10 * sizeof(gpu_card_mask_t);
    size_t results_size = max_batch_size * 10 * sizeof(HandVal);
    ctx->d_boards = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY, board_size, NULL, &err);
    ctx->d_hole_cards = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY, hole_cards_size, NULL, &err);
    ctx->d_results = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, results_size, NULL, &err);
    ctx->boards_capacity = max_batch_size;
    ctx->hole_cards_capacity = max_batch_size * 10;
    ctx->results_capacity = max_batch_size * 10;

    return (gpu_eval_context_t *)ctx;
}

/* Cleanup OpenCL context */
void opencl_gpu_eval_cleanup(gpu_eval_context_t *ctx)
{
    if (!ctx)
        return;
    opencl_eval_context_t *ocl_ctx = (opencl_eval_context_t *)ctx;
    if (ocl_ctx->d_boards)
        clReleaseMemObject(ocl_ctx->d_boards);
    if (ocl_ctx->d_hole_cards)
        clReleaseMemObject(ocl_ctx->d_hole_cards);
    if (ocl_ctx->d_results)
        clReleaseMemObject(ocl_ctx->d_results);
    if (ocl_ctx->d_nBitsTable)
        clReleaseMemObject(ocl_ctx->d_nBitsTable);
    if (ocl_ctx->d_straightTable)
        clReleaseMemObject(ocl_ctx->d_straightTable);
    if (ocl_ctx->d_topFiveCardsTable)
        clReleaseMemObject(ocl_ctx->d_topFiveCardsTable);
    if (ocl_ctx->d_topCardTable)
        clReleaseMemObject(ocl_ctx->d_topCardTable);
    if (ocl_ctx->eval_kernel)
        clReleaseKernel(ocl_ctx->eval_kernel);
    if (ocl_ctx->program)
        clReleaseProgram(ocl_ctx->program);
    if (ocl_ctx->queue)
        clReleaseCommandQueue(ocl_ctx->queue);
    if (ocl_ctx->context)
        clReleaseContext(ocl_ctx->context);
    free(ocl_ctx);
}

/* Batch evaluate boards */
int opencl_gpu_eval_batch_boards(
    gpu_eval_context_t *ctx,
    StdDeck_CardMask *boards,
    StdDeck_CardMask *hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t *result)
{
    if (!ctx || ctx->backend_type != 1)
        return -1;
    opencl_eval_context_t *ocl_ctx = (opencl_eval_context_t *)ctx;
    cl_int err;
    if (n_boards > ocl_ctx->boards_capacity || n_boards * n_players > ocl_ctx->hole_cards_capacity)
        return -1;

    /* Convert boards to GPU format */
    gpu_card_mask_t *gpu_boards = malloc(n_boards * sizeof(gpu_card_mask_t));
    if (!gpu_boards)
        return -1;
    for (int i = 0; i < n_boards; i++)
    {
        convert_to_gpu_mask(&boards[i], &gpu_boards[i]);
    }

    /* Convert hole cards to GPU format */
    gpu_card_mask_t *gpu_hole_cards = malloc(n_boards * n_players * sizeof(gpu_card_mask_t));
    if (!gpu_hole_cards)
    {
        free(gpu_boards);
        return -1;
    }
    for (int i = 0; i < n_boards * n_players; i++)
    {
        convert_to_gpu_mask(&hole_cards[i], &gpu_hole_cards[i]);
    }

    err = clEnqueueWriteBuffer(ocl_ctx->queue, ocl_ctx->d_boards, CL_TRUE, 0, n_boards * sizeof(gpu_card_mask_t), gpu_boards, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        free(gpu_boards);
        free(gpu_hole_cards);
        return -1;
    }
    err = clEnqueueWriteBuffer(ocl_ctx->queue, ocl_ctx->d_hole_cards, CL_TRUE, 0, n_boards * n_players * sizeof(gpu_card_mask_t), gpu_hole_cards, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        free(gpu_boards);
        free(gpu_hole_cards);
        return -1;
    }
    err = clSetKernelArg(ocl_ctx->eval_kernel, 0, sizeof(cl_mem), &ocl_ctx->d_boards);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 1, sizeof(cl_mem), &ocl_ctx->d_hole_cards);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 2, sizeof(int), &n_boards);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 3, sizeof(int), &n_players);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 4, sizeof(cl_mem), &ocl_ctx->d_results);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 5, sizeof(cl_mem), &ocl_ctx->d_nBitsTable);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 6, sizeof(cl_mem), &ocl_ctx->d_straightTable);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 7, sizeof(cl_mem), &ocl_ctx->d_topFiveCardsTable);
    err |= clSetKernelArg(ocl_ctx->eval_kernel, 8, sizeof(cl_mem), &ocl_ctx->d_topCardTable);
    if (err != CL_SUCCESS)
    {
        free(gpu_boards);
        free(gpu_hole_cards);
        return -1;
    }
    size_t global_work_size = n_boards;
    err = clEnqueueNDRangeKernel(ocl_ctx->queue, ocl_ctx->eval_kernel, 1, NULL, &global_work_size, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        free(gpu_boards);
        free(gpu_hole_cards);
        return -1;
    }
    if (!result->hand_values)
    {
        result->hand_values = (HandVal *)malloc(n_boards * n_players * sizeof(HandVal));
        if (!result->hand_values)
        {
            free(gpu_boards);
            free(gpu_hole_cards);
            return -1;
        }
    }
    err = clEnqueueReadBuffer(ocl_ctx->queue, ocl_ctx->d_results, CL_TRUE, 0, n_boards * n_players * sizeof(HandVal), result->hand_values, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        free(gpu_boards);
        free(gpu_hole_cards);
        return -1;
    }
    result->batch_size = n_boards * n_players;

    /* Cleanup temporary buffers */
    free(gpu_boards);
    free(gpu_hole_cards);

    return 0;
}

/* Monte Carlo equity calculation */
int opencl_gpu_monte_carlo_equity(
    gpu_eval_context_t *ctx,
    StdDeck_CardMask *ranges,
    int n_players,
    int n_simulations,
    float *equities)
{
    if (!ctx || ctx->backend_type != 1)
        return -1;
    /* For now, implement a simple CPU-based Monte Carlo as placeholder */
    /* This gives us working functionality while we develop the full GPU version */

    int *wins = calloc(n_players, sizeof(int));
    int *ties = calloc(n_players, sizeof(int));
    if (!wins || !ties)
    {
        free(wins);
        free(ties);
        return -1;
    }

    for (int sim = 0; sim < n_simulations; sim++)
    {
        /* Generate random hands for each player */
        StdDeck_CardMask player_hands[10];
        StdDeck_CardMask used_cards;
        StdDeck_CardMask_RESET(used_cards);

        /* Deal hole cards */
        for (int p = 0; p < n_players && p < 10; p++)
        {
            StdDeck_CardMask_RESET(player_hands[p]);
            for (int c = 0; c < 2; c++)
            {
                int card;
                StdDeck_CardMask card_mask;
                do
                {
                    card = rand() % 52;
                    card_mask = StdDeck_MASK(card);
                } while (StdDeck_CardMask_ANY_SET(used_cards, card_mask));

                StdDeck_CardMask_OR(player_hands[p], player_hands[p], card_mask);
                StdDeck_CardMask_OR(used_cards, used_cards, card_mask);
            }
        }

        /* Deal board */
        StdDeck_CardMask board;
        StdDeck_CardMask_RESET(board);
        for (int c = 0; c < 5; c++)
        {
            int card;
            StdDeck_CardMask card_mask;
            do
            {
                card = rand() % 52;
                card_mask = StdDeck_MASK(card);
            } while (StdDeck_CardMask_ANY_SET(used_cards, card_mask));

            StdDeck_CardMask_OR(board, board, card_mask);
            StdDeck_CardMask_OR(used_cards, used_cards, card_mask);
        }

        /* Evaluate all hands */
        HandVal hand_values[10];
        for (int p = 0; p < n_players && p < 10; p++)
        {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, player_hands[p], board);
            hand_values[p] = StdDeck_StdRules_EVAL_N(combined, 7);
        }

        /* Find winner(s) */
        HandVal best_hand = 0;
        int winner_count = 0;

        for (int p = 0; p < n_players; p++)
        {
            if (hand_values[p] > best_hand)
            {
                best_hand = hand_values[p];
                winner_count = 1;
            }
            else if (hand_values[p] == best_hand)
            {
                winner_count++;
            }
        }

        /* Update win/tie counts */
        for (int p = 0; p < n_players; p++)
        {
            if (hand_values[p] == best_hand)
            {
                if (winner_count == 1)
                {
                    wins[p]++;
                }
                else
                {
                    ties[p]++;
                }
            }
        }
    }

    /* Calculate equities */
    for (int p = 0; p < n_players; p++)
    {
        equities[p] = (float)(wins[p] + ties[p] * 0.5) / (float)n_simulations;
    }

    free(wins);
    free(ties);
    return 0;
}

/* Get device information */
int opencl_gpu_get_device_info(int device_id, char *name, size_t *memory, int *compute_units)
{
    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint num_platforms;
    cl_device_id devices[MAX_DEVICES];
    cl_uint num_devices;
    cl_int err = clGetPlatformIDs(MAX_PLATFORMS, platforms, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0)
        return -1;
    err = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0)
        return -1;
    if (device_id >= num_devices)
        return -1;
    cl_device_id dev = devices[device_id];
    if (name)
    {
        char dev_name[256];
        clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(dev_name), dev_name, NULL);
        snprintf(name, 256, "%s", dev_name);
    }
    if (memory)
    {
        cl_ulong mem;
        clGetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(mem), &mem, NULL);
        *memory = (size_t)mem;
    }
    if (compute_units)
    {
        cl_uint cus;
        clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, NULL);
        *compute_units = (int)cus;
    }
    return 0;
}

/* Check if OpenCL is available */
int opencl_gpu_is_available(int backend_type)
{
    if (backend_type != 1)
        return 0;
    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint num_platforms;
    cl_int err = clGetPlatformIDs(MAX_PLATFORMS, platforms, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0)
        return 0;
    cl_device_id devices[MAX_DEVICES];
    cl_uint num_devices;
    err = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0)
        return 0;
    return 1;
}

/* ========================================================================
 * Multi-Game Support Functions (OpenCL)
 * ======================================================================== */

/* Extended OpenCL context for multi-game support */
typedef struct
{
    opencl_eval_context_t base_ctx;
    cl_kernel generic_eval_kernel;
    cl_mem d_game_config;
    cl_mem d_results_lo;
    cl_mem d_lo_qualifies;
    gpu_game_config_t current_config;
} opencl_multigame_context_t;

/* Load and compile kernel source from multiple files */
static char* load_multi_kernel_sources(const char** filenames, int num_files)
{
    size_t total_size = 0;
    char* sources[16];  /* Max 16 kernel files */

    if (num_files > 16) return NULL;

    /* Load each file */
    for (int i = 0; i < num_files; i++) {
        sources[i] = load_kernel_source(filenames[i]);
        if (!sources[i]) {
            opencl_debug_log("Kernel '%s' could not be loaded", filenames[i]);
            /* Free previously loaded sources */
            for (int j = 0; j < i; j++) {
                free(sources[j]);
            }
            return NULL;
        }
        total_size += strlen(sources[i]) + 1; /* +1 for newline */
    }

    /* Combine all sources */
    char* combined = (char*)malloc(total_size + 1);
    if (!combined) {
        for (int i = 0; i < num_files; i++) {
            free(sources[i]);
        }
        opencl_debug_log("Failed to allocate combined kernel buffer (%zu bytes requested)", total_size + 1);
        return NULL;
    }

    size_t offset = 0;
    for (int i = 0; i < num_files; i++) {
        size_t remaining = total_size + 1 - offset;
        int written = snprintf(combined + offset, remaining, "%s\n", sources[i]);
        free(sources[i]);
        if (written < 0 || (size_t)written >= remaining) {
            for (int j = i + 1; j < num_files; j++) {
                free(sources[j]);
            }
            free(combined);
            opencl_debug_log("Failed to combine OpenCL kernel sources");
            return NULL;
        }
        offset += (size_t)written;
    }

    opencl_debug_log("Combined %d kernel source files into %zu bytes", num_files, total_size);
    return combined;
}

/*
 * Initialize OpenCL context with game configuration
 * This is the multi-game equivalent of opencl_gpu_eval_init()
 */
gpu_eval_context_t* opencl_gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t config
) {
    if (backend_type != 1) {
        return NULL; /* Only OpenCL backend */
    }

    cl_int err;
    opencl_multigame_context_t* ctx =
        (opencl_multigame_context_t*)calloc(1, sizeof(opencl_multigame_context_t));
    if (!ctx) return NULL;

    /* Save game config */
    ctx->current_config = config;

    /* Get platform and device */
    cl_platform_id platforms[MAX_PLATFORMS];
    cl_uint num_platforms;
    err = clGetPlatformIDs(MAX_PLATFORMS, platforms, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        free(ctx);
        return NULL;
    }

    cl_device_id devices[MAX_DEVICES];
    cl_uint num_devices;
    err = clGetDeviceIDs(platforms[0], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0) {
        free(ctx);
        return NULL;
    }

    if (device_id >= (int)num_devices) {
        free(ctx);
        return NULL;
    }

    ctx->base_ctx.platform = platforms[0];
    ctx->base_ctx.device = devices[device_id];

    /* Create context and command queue */
    ctx->base_ctx.context = clCreateContext(NULL, 1, &ctx->base_ctx.device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        free(ctx);
        return NULL;
    }

    ctx->base_ctx.queue = opencl_create_command_queue(
        ctx->base_ctx.context, ctx->base_ctx.device, &err);
    if (err != CL_SUCCESS) {
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    /* Load and compile multi-game kernels */
    const char* kernel_files[] = {
        "eval_kernel.cl",           /* Base eval - load_kernel_source will search paths */
        "eval_low_kernel.cl",       /* Low evaluation */
        "eval_omaha_kernel.cl",     /* Omaha selection */
        "eval_generic_kernel.cl"    /* Generic multi-game */
    };

    char* source = load_multi_kernel_sources(kernel_files, 4);
    if (!source) {
        clReleaseCommandQueue(ctx->base_ctx.queue);
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    const char* source_ptr = source;
    ctx->base_ctx.program = clCreateProgramWithSource(
        ctx->base_ctx.context, 1, &source_ptr, NULL, &err);
    free(source);

    if (err != CL_SUCCESS) {
        clReleaseCommandQueue(ctx->base_ctx.queue);
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    /* Build program with fast optimizations (relaxed math maintains correctness for poker eval) */
    const char* build_options = "-cl-mad-enable -cl-fast-relaxed-math";
    err = clBuildProgram(ctx->base_ctx.program, 1, &ctx->base_ctx.device, build_options, NULL, NULL);
    if (err != CL_SUCCESS) {
        print_opencl_build_log(ctx->base_ctx.program, ctx->base_ctx.device, err);

        clReleaseProgram(ctx->base_ctx.program);
        clReleaseCommandQueue(ctx->base_ctx.queue);
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    /* Create generic evaluation kernel */
    ctx->generic_eval_kernel = clCreateKernel(ctx->base_ctx.program,
                                               "eval_generic_boards_kernel", &err);
    if (err != CL_SUCCESS) {
        clReleaseProgram(ctx->base_ctx.program);
        clReleaseCommandQueue(ctx->base_ctx.queue);
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    /* Allocate device memory for lookup tables (copied once) */
    ctx->base_ctx.d_nBitsTable = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        8192 * sizeof(uint8_t), nBitsTable, &err);
    ctx->base_ctx.d_straightTable = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        8192 * sizeof(uint8_t), straightTable, &err);
    ctx->base_ctx.d_topFiveCardsTable = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        8192 * sizeof(uint32_t), topFiveCardsTable, &err);
    ctx->base_ctx.d_topCardTable = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        8192 * sizeof(uint8_t), topCardTable, &err);

    if (err != CL_SUCCESS) {
        clReleaseKernel(ctx->generic_eval_kernel);
        clReleaseProgram(ctx->base_ctx.program);
        clReleaseCommandQueue(ctx->base_ctx.queue);
        clReleaseContext(ctx->base_ctx.context);
        free(ctx);
        return NULL;
    }

    /* Allocate device memory buffers */
    size_t board_size = max_batch_size * sizeof(gpu_card_mask_t);
    size_t hole_size = max_batch_size * 10 * sizeof(gpu_card_mask_t); /* Max 10 players */
    size_t result_size = max_batch_size * 10 * sizeof(uint32_t);

    ctx->base_ctx.d_boards = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY, board_size, NULL, &err);
    ctx->base_ctx.d_hole_cards = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY, hole_size, NULL, &err);
    ctx->base_ctx.d_results = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_WRITE_ONLY, result_size, NULL, &err);
    ctx->d_results_lo = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_WRITE_ONLY, result_size, NULL, &err);
    ctx->d_lo_qualifies = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_WRITE_ONLY, result_size, NULL, &err);
    ctx->d_game_config = clCreateBuffer(ctx->base_ctx.context,
        CL_MEM_READ_ONLY, sizeof(gpu_game_config_t), NULL, &err);

    /* Upload game config */
    clEnqueueWriteBuffer(ctx->base_ctx.queue, ctx->d_game_config, CL_TRUE,
                        0, sizeof(gpu_game_config_t), &config, 0, NULL, NULL);

    /* Store capacities */
    ctx->base_ctx.boards_capacity = max_batch_size;
    ctx->base_ctx.hole_cards_capacity = max_batch_size * 10;
    ctx->base_ctx.results_capacity = max_batch_size * 10;

    /* Set base fields */
    ctx->base_ctx.base.backend_type = 1; /* OpenCL */
    ctx->base_ctx.base.device_id = device_id;
    ctx->base_ctx.base.max_batch_size = max_batch_size;
    ctx->base_ctx.base.game_config = config;

    return (gpu_eval_context_t*)ctx;
}

/*
 * Batch evaluate boards with hi/lo support
 */
int opencl_gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx_base,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
) {
    if (!ctx_base || !result) return -1;

    opencl_multigame_context_t* ctx = (opencl_multigame_context_t*)ctx_base;

    /* Convert card masks to GPU format */
    int total_hands = n_boards * n_players;
    gpu_card_mask_t* gpu_boards = (gpu_card_mask_t*)malloc(n_boards * sizeof(gpu_card_mask_t));
    gpu_card_mask_t* gpu_holes = (gpu_card_mask_t*)malloc(total_hands * sizeof(gpu_card_mask_t));

    for (int i = 0; i < n_boards; i++) {
        convert_to_gpu_mask(&boards[i], &gpu_boards[i]);
    }
    for (int i = 0; i < total_hands; i++) {
        convert_to_gpu_mask(&hole_cards[i], &gpu_holes[i]);
        /* Debug 2-7 lowball first hand */
        if (i == 0 && ctx->current_config.game == GPU_GAME_LOWBALL27) {
            printf("    GPU hole[0] suits: S=%04x C=%04x D=%04x H=%04x\n",
                   gpu_holes[i].spades, gpu_holes[i].clubs,
                   gpu_holes[i].diamonds, gpu_holes[i].hearts);
        }
    }

    /* Upload to device */
    cl_int err;
    err = clEnqueueWriteBuffer(ctx->base_ctx.queue, ctx->base_ctx.d_boards, CL_TRUE,
                               0, n_boards * sizeof(gpu_card_mask_t), gpu_boards, 0, NULL, NULL);
    err |= clEnqueueWriteBuffer(ctx->base_ctx.queue, ctx->base_ctx.d_hole_cards, CL_TRUE,
                                0, total_hands * sizeof(gpu_card_mask_t), gpu_holes, 0, NULL, NULL);

    free(gpu_boards);
    free(gpu_holes);

    if (err != CL_SUCCESS) return -1;

    /* Set kernel arguments - lookup tables MUST always be passed as kernel expects them */
    int arg_idx = 0;
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_boards);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_hole_cards);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(int), &n_boards);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(int), &n_players);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->d_game_config);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_results);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->d_results_lo);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->d_lo_qualifies);

    /* Lookup table arguments (kernel always expects these) */
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_nBitsTable);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_straightTable);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_topFiveCardsTable);
    clSetKernelArg(ctx->generic_eval_kernel, arg_idx++, sizeof(cl_mem), &ctx->base_ctx.d_topCardTable);

    /* Launch kernel */
    size_t global_work_size = n_boards;
    err = clEnqueueNDRangeKernel(ctx->base_ctx.queue, ctx->generic_eval_kernel,
                                 1, NULL, &global_work_size, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) return -1;

    /* Read back results */
    uint32_t* hi_vals = (uint32_t*)malloc(total_hands * sizeof(uint32_t));
    uint32_t* lo_vals = (uint32_t*)malloc(total_hands * sizeof(uint32_t));
    int* lo_qual = (int*)malloc(total_hands * sizeof(int));

    err = clEnqueueReadBuffer(ctx->base_ctx.queue, ctx->base_ctx.d_results, CL_TRUE,
                             0, total_hands * sizeof(uint32_t), hi_vals, 0, NULL, NULL);
    err |= clEnqueueReadBuffer(ctx->base_ctx.queue, ctx->d_results_lo, CL_TRUE,
                              0, total_hands * sizeof(uint32_t), lo_vals, 0, NULL, NULL);
    err |= clEnqueueReadBuffer(ctx->base_ctx.queue, ctx->d_lo_qualifies, CL_TRUE,
                              0, total_hands * sizeof(int), lo_qual, 0, NULL, NULL);

    if (err != CL_SUCCESS) {
        free(hi_vals);
        free(lo_vals);
        free(lo_qual);
        return -1;
    }

    /* Ensure caller buffers are allocated */
    if (!result->hand_values_hi) {
        result->hand_values_hi = (HandVal*)malloc(total_hands * sizeof(HandVal));
    }

    if (ctx->current_config.eval_low) {
        if (!result->hand_values_lo) {
            result->hand_values_lo = (HandVal*)malloc(total_hands * sizeof(HandVal));
        }
        if (!result->lo_qualifies) {
            result->lo_qualifies = (int*)malloc(total_hands * sizeof(int));
        }
    }

    if (!result->hand_values_hi ||
        (ctx->current_config.eval_low && (!result->hand_values_lo || !result->lo_qualifies))) {
        free(hi_vals);
        free(lo_vals);
        free(lo_qual);
        return -1;
    }

    /* Fill result structure */
    result->batch_size = total_hands;
    for (int i = 0; i < total_hands; i++) {
        result->hand_values_hi[i] = hi_vals[i];
        if (ctx->current_config.eval_low) {
            result->hand_values_lo[i] = lo_vals[i];
            result->lo_qualifies[i] = lo_qual[i];
        }
    }

    free(hi_vals);
    free(lo_vals);
    free(lo_qual);

    return 0;
}
