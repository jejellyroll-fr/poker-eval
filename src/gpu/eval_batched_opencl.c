/*
 * eval_batched_opencl.c - OpenCL backend for batched GPU evaluation
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Implements OpenCL backend for GPU-accelerated poker hand evaluation.
 * Compatible with AMD, Intel, and NVIDIA GPUs via OpenCL 1.2+
 *
 * Uses poker-eval lookup tables loaded via gpu_table_loader.c to mirror
 * the CPU StdDeck_StdRules_EVAL_N evaluation logic on GPU.
 */

#include "eval_batched_opencl.h"
#include "gpu_table_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

/* OpenCL context structure */
struct gpu_eval_context_opencl_t {
    cl_platform_id platform;
    cl_device_id device;
    cl_device_type device_type;
    int device_index;
    cl_context context;
    cl_command_queue queue;

    /* Compiled kernels */
    cl_program program;
    cl_kernel kernel_basic;
    cl_kernel kernel_opt;
    cl_kernel kernel_equity;
    cl_kernel kernel_omaha;
    cl_kernel kernel_omaha5;
    cl_kernel kernel_omaha6;

    /* Multi-game kernels */
    cl_kernel kernel_stud;
    cl_kernel kernel_razz;
    cl_kernel kernel_omaha_hilo;
    cl_kernel kernel_hilo;

    /* Device buffers */
    cl_mem d_hands;
    cl_mem d_values;
    cl_mem d_boards;
    cl_mem d_wins;
    cl_mem d_ties;
    cl_mem d_losses;

    /* Hi/Lo output buffers */
    cl_mem d_hi_values;
    cl_mem d_lo_values;
    cl_mem d_lo_qualifies;

    /* Lookup table buffers (mirrors StdDeck_StdRules_EVAL_N tables) */
    cl_mem d_nbits_table;
    cl_mem d_straight_table;
    cl_mem d_top_card_table;
    cl_mem d_top_five_table;

    /* Low evaluation table buffers */
    cl_mem d_bottom_five_table;
    cl_mem d_bottom_card_table;

    /* Buffer sizes */
    size_t hands_buffer_size;
    size_t values_buffer_size;
    size_t boards_buffer_size;

    /* Profiling events */
    cl_event last_event;

    /* Device properties */
    size_t max_work_group_size;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;
    int compute_units;

    char device_name[256];
    char device_vendor[256];
};

/*
 * Error checking macros.
 *
 * The internal variable must not be named err: every caller passes a variable
 * of that name, so the expansion used to read `cl_int err = (err)`, shadowing
 * the caller's variable and initialising it from itself. The value tested was
 * indeterminate and the real OpenCL return codes were never examined.
 */
#define CL_CHECK(call) do { \
    cl_int cl_check_status_ = (call); \
    if (cl_check_status_ != CL_SUCCESS) { \
        fprintf(stderr, "OpenCL error %d at %s:%d\n", cl_check_status_, __FILE__, __LINE__); \
        return NULL; \
    } \
} while(0)

#define CL_CHECK_ERR(call, ret_val) do { \
    cl_int cl_check_status_ = (call); \
    if (cl_check_status_ != CL_SUCCESS) { \
        fprintf(stderr, "OpenCL error %d at %s:%d\n", cl_check_status_, __FILE__, __LINE__); \
        return ret_val; \
    } \
} while(0)

/* Read kernel source from file */
static char* read_kernel_source(const char* filename, size_t* length) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Failed to open kernel file: %s\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = (char*)malloc(size + 1);
    if (!source) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    if (length) *length = read;
    return source;
}

/**
 * Initialize OpenCL backend
 */
gpu_eval_context_t* gpu_eval_init_opencl(const gpu_eval_config_t* config) {
    cl_int err;

    /* Allocate context */
    gpu_eval_context_opencl_t* ctx =
        (gpu_eval_context_opencl_t*)calloc(1, sizeof(gpu_eval_context_opencl_t));
    if (!ctx) return NULL;

    /* Enumerate platforms */
    cl_uint num_platforms = 0;
    err = clGetPlatformIDs(0, NULL, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        fprintf(stderr, "No OpenCL platforms found\n");
        free(ctx);
        return NULL;
    }

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    if (!platforms) {
        free(ctx);
        return NULL;
    }
    err = clGetPlatformIDs(num_platforms, platforms, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to enumerate OpenCL platforms (err=%d)\n", err);
        free(platforms);
        free(ctx);
        return NULL;
    }

    typedef struct {
        cl_platform_id platform;
        cl_device_id device;
        cl_device_type type;
    } device_entry_t;

    device_entry_t* entries = NULL;
    size_t entry_count = 0;

    cl_device_type device_types[2] = {
        CL_DEVICE_TYPE_GPU,
        CL_DEVICE_TYPE_CPU
    };

    for (int t = 0; t < 2; ++t) {
        cl_device_type dev_type = device_types[t];

        for (cl_uint p = 0; p < num_platforms; ++p) {
            cl_uint count = 0;
            cl_int local_err = clGetDeviceIDs(platforms[p], dev_type, 0, NULL, &count);
            if (local_err == CL_DEVICE_NOT_FOUND || count == 0) {
                continue;
            }
            if (local_err != CL_SUCCESS) {
                fprintf(stderr, "clGetDeviceIDs failed (err=%d)\n", local_err);
                free(platforms);
                free(entries);
                free(ctx);
                return NULL;
            }

            cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * count);
            if (!devices) {
                free(platforms);
                free(entries);
                free(ctx);
                return NULL;
            }
            local_err = clGetDeviceIDs(platforms[p], dev_type, count, devices, NULL);
            if (local_err != CL_SUCCESS) {
                fprintf(stderr, "clGetDeviceIDs(list) failed (err=%d)\n", local_err);
                free(devices);
                free(platforms);
                free(entries);
                free(ctx);
                return NULL;
            }

            device_entry_t* grown = (device_entry_t*)realloc(entries, sizeof(device_entry_t) * (entry_count + count));
            if (!grown) {
                free(devices);
                free(platforms);
                free(entries);
                free(ctx);
                return NULL;
            }
            entries = grown;

            for (cl_uint i = 0; i < count; ++i) {
                entries[entry_count + i].platform = platforms[p];
                entries[entry_count + i].device = devices[i];
                entries[entry_count + i].type = dev_type;
            }
            entry_count += count;
            free(devices);
        }

        if (entry_count > 0) {
            break; /* Prefer GPUs; fall back to CPU only if none found */
        }
    }

    free(platforms);

    if (entry_count == 0) {
        fprintf(stderr, "No suitable OpenCL devices found\n");
        free(entries);
        free(ctx);
        return NULL;
    }

    int desired_index = (config && config->device_id >= 0) ? config->device_id : 0;
    if (desired_index >= (int)entry_count) {
        desired_index = (int)entry_count - 1;
    }

    ctx->platform = entries[desired_index].platform;
    ctx->device = entries[desired_index].device;
    ctx->device_type = entries[desired_index].type;
    ctx->device_index = desired_index;
    free(entries);

    /* Query device properties */
    clGetDeviceInfo(ctx->device, CL_DEVICE_NAME, sizeof(ctx->device_name),
                    ctx->device_name, NULL);
    clGetDeviceInfo(ctx->device, CL_DEVICE_VENDOR, sizeof(ctx->device_vendor),
                    ctx->device_vendor, NULL);
    clGetDeviceInfo(ctx->device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(ctx->max_work_group_size), &ctx->max_work_group_size, NULL);
    clGetDeviceInfo(ctx->device, CL_DEVICE_GLOBAL_MEM_SIZE,
                    sizeof(ctx->global_mem_size), &ctx->global_mem_size, NULL);
    clGetDeviceInfo(ctx->device, CL_DEVICE_LOCAL_MEM_SIZE,
                    sizeof(ctx->local_mem_size), &ctx->local_mem_size, NULL);
    clGetDeviceInfo(ctx->device, CL_DEVICE_MAX_COMPUTE_UNITS,
                    sizeof(ctx->compute_units), &ctx->compute_units, NULL);

    if (config->verbose) {
        printf("OpenCL Device: %s (%s)\n", ctx->device_name, ctx->device_vendor);
        printf("Compute Units: %d\n", ctx->compute_units);
        printf("Max Work Group Size: %zu\n", ctx->max_work_group_size);
        printf("Global Memory: %.2f MB\n", (double)ctx->global_mem_size / (1024.0 * 1024.0));
        printf("Local Memory: %.2f KB\n", (double)ctx->local_mem_size / 1024.0);
    }

    /* Create context */
    ctx->context = clCreateContext(NULL, 1, &ctx->device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create OpenCL context\n");
        free(ctx);
        return NULL;
    }

    /* Create command queue with profiling enabled */
    cl_command_queue_properties props = CL_QUEUE_PROFILING_ENABLE;
    ctx->queue = clCreateCommandQueue(ctx->context, ctx->device, props, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create command queue\n");
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Load and compile kernel */
    size_t source_len;
    char* kernel_source = read_kernel_source("gpu/kernels/eval_holdem_batch.cl", &source_len);
    if (!kernel_source) {
        /* Try alternate paths */
        kernel_source = read_kernel_source("../gpu/kernels/eval_holdem_batch.cl", &source_len);
        if (!kernel_source) {
            kernel_source = read_kernel_source("src/gpu/kernels/eval_holdem_batch.cl", &source_len);
            if (!kernel_source) {
                fprintf(stderr, "Failed to load OpenCL kernel source\n");
                clReleaseCommandQueue(ctx->queue);
                clReleaseContext(ctx->context);
                free(ctx);
                return NULL;
            }
        }
    }

    /* clCreateProgramWithSource takes const char**; casting &kernel_source
     * directly would add const at the inner level only, which is unsafe. */
    const char* const_kernel_source = kernel_source;
    ctx->program = clCreateProgramWithSource(ctx->context, 1,
                                             &const_kernel_source, &source_len, &err);
    free(kernel_source);

    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create program\n");
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Build program */
    err = clBuildProgram(ctx->program, 1, &ctx->device, "-cl-fast-relaxed-math", NULL, NULL);
    if (err != CL_SUCCESS) {
        /* Print build log */
        size_t log_size;
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG,
                             0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG,
                             log_size, log, NULL);
        fprintf(stderr, "OpenCL build error:\n%s\n", log);
        free(log);

        clReleaseProgram(ctx->program);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Create kernels */
    ctx->kernel_basic = clCreateKernel(ctx->program, "eval_holdem_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create basic kernel (err=%d)\n", err);
        clReleaseProgram(ctx->program);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->kernel_opt = clCreateKernel(ctx->program, "eval_holdem_batch_kernel_opt", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create optimized kernel (err=%d)\n", err);
        ctx->kernel_opt = NULL;
    }

    ctx->kernel_equity = clCreateKernel(ctx->program, "eval_equity_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create equity kernel (err=%d)\n", err);
        ctx->kernel_equity = NULL;
    }

    ctx->kernel_omaha = clCreateKernel(ctx->program, "eval_omaha_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Omaha kernel (err=%d)\n", err);
        ctx->kernel_omaha = NULL;
    }

    ctx->kernel_omaha5 = clCreateKernel(ctx->program, "eval_omaha5_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Omaha5 kernel (err=%d)\n", err);
        ctx->kernel_omaha5 = NULL;
    }

    ctx->kernel_omaha6 = clCreateKernel(ctx->program, "eval_omaha6_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Omaha6 kernel (err=%d)\n", err);
        ctx->kernel_omaha6 = NULL;
    }

    /* Create multi-game kernels */
    ctx->kernel_stud = clCreateKernel(ctx->program, "eval_stud_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Stud kernel (err=%d)\n", err);
        ctx->kernel_stud = NULL;
    }

    ctx->kernel_razz = clCreateKernel(ctx->program, "eval_razz_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Razz kernel (err=%d)\n", err);
        ctx->kernel_razz = NULL;
    }

    ctx->kernel_omaha_hilo = clCreateKernel(ctx->program, "eval_omaha_hilo_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create Omaha Hi/Lo kernel (err=%d)\n", err);
        ctx->kernel_omaha_hilo = NULL;
    }

    ctx->kernel_hilo = clCreateKernel(ctx->program, "eval_hilo_batch_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Warning: Failed to create generic Hi/Lo kernel (err=%d)\n", err);
        ctx->kernel_hilo = NULL;
    }

    /* Allocate device buffers */
    size_t max_batch = config->max_batch_size > 0 ? config->max_batch_size : 1000000;

    ctx->hands_buffer_size = max_batch * 11 * sizeof(uint8_t); /* 11 for Omaha6 (6 hole + 5 board) */
    ctx->values_buffer_size = max_batch * sizeof(uint32_t);
    ctx->boards_buffer_size = max_batch * 5 * sizeof(uint8_t);

    ctx->d_hands = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY,
                                  ctx->hands_buffer_size, NULL, &err);
    ctx->d_values = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                   ctx->values_buffer_size, NULL, &err);
    ctx->d_boards = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY,
                                   ctx->boards_buffer_size, NULL, &err);

    /* Allocate counter buffers for equity */
    ctx->d_wins = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE,
                                 sizeof(uint64_t), NULL, &err);
    ctx->d_ties = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE,
                                 sizeof(uint64_t), NULL, &err);
    ctx->d_losses = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE,
                                   sizeof(uint64_t), NULL, &err);

    /* Allocate Hi/Lo output buffers */
    ctx->d_hi_values = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                      ctx->values_buffer_size, NULL, &err);
    ctx->d_lo_values = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                      ctx->values_buffer_size, NULL, &err);
    ctx->d_lo_qualifies = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                         max_batch * sizeof(int), NULL, &err);

    /* Load poker-eval lookup tables for 5-card evaluation
     * These tables mirror StdDeck_StdRules_EVAL_N logic on GPU */
    /* Heap rather than stack: the two lookup-table structs total about 96 KB,
     * which overruns the project's -Wstack-usage=65536 limit. They are staging
     * buffers, read once into device memory and then dropped. */
    gpu_lookup_tables_t *tables = (gpu_lookup_tables_t*)malloc(sizeof(*tables));
    if (!tables) {
        fprintf(stderr, "Failed to allocate lookup table staging buffer\n");
        gpu_eval_free_opencl((gpu_eval_context_t*)ctx);
        return NULL;
    }
    if (gpu_load_all_tables(tables) != 0) {
        fprintf(stderr, "Warning: Failed to load poker-eval tables\n");
        memset(tables, 0, sizeof(*tables));
    } else {
        if (gpu_validate_tables(tables) != 0) {
            fprintf(stderr, "Warning: Loaded tables failed validation\n");
        }
        if (config->verbose) {
            gpu_print_table_stats(tables);
        }
    }

    /* Create buffers and copy lookup tables */
    ctx->d_nbits_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        sizeof(tables->nbits_table), tables->nbits_table, &err);
    ctx->d_straight_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           sizeof(tables->straight_table), tables->straight_table, &err);
    ctx->d_top_card_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           sizeof(tables->top_card_table), tables->top_card_table, &err);
    ctx->d_top_five_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           sizeof(tables->top_five_table), tables->top_five_table, &err);
    free(tables);

    /* Load low evaluation tables (for Razz / Hi-Lo) */
    gpu_low_lookup_tables_t *low_tables = (gpu_low_lookup_tables_t*)malloc(sizeof(*low_tables));
    if (!low_tables) {
        fprintf(stderr, "Failed to allocate low lookup table staging buffer\n");
        gpu_eval_free_opencl((gpu_eval_context_t*)ctx);
        return NULL;
    }
    if (gpu_load_low_tables(low_tables) != 0) {
        fprintf(stderr, "Warning: Failed to load low evaluation tables\n");
        memset(low_tables, 0, sizeof(*low_tables));
    } else {
        if (gpu_validate_low_tables(low_tables) != 0) {
            fprintf(stderr, "Warning: Low tables failed validation\n");
        }
    }

    ctx->d_bottom_five_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                              sizeof(low_tables->bottom_five_table),
                                              low_tables->bottom_five_table, &err);
    ctx->d_bottom_card_table = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                              sizeof(low_tables->bottom_card_table),
                                              low_tables->bottom_card_table, &err);
    free(low_tables);

    if (config->verbose) {
        printf("OpenCL backend initialized successfully\n");
        printf("Lookup tables loaded to device memory (high + low)\n");
    }

    return (gpu_eval_context_t*)ctx;
}

/**
 * Cleanup OpenCL resources
 */
void gpu_eval_free_opencl(gpu_eval_context_t* ctx_opaque) {
    if (!ctx_opaque) return;

    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;

    if (ctx->d_hands) clReleaseMemObject(ctx->d_hands);
    if (ctx->d_values) clReleaseMemObject(ctx->d_values);
    if (ctx->d_boards) clReleaseMemObject(ctx->d_boards);
    if (ctx->d_wins) clReleaseMemObject(ctx->d_wins);
    if (ctx->d_ties) clReleaseMemObject(ctx->d_ties);
    if (ctx->d_losses) clReleaseMemObject(ctx->d_losses);
    if (ctx->d_hi_values) clReleaseMemObject(ctx->d_hi_values);
    if (ctx->d_lo_values) clReleaseMemObject(ctx->d_lo_values);
    if (ctx->d_lo_qualifies) clReleaseMemObject(ctx->d_lo_qualifies);
    if (ctx->d_nbits_table) clReleaseMemObject(ctx->d_nbits_table);
    if (ctx->d_straight_table) clReleaseMemObject(ctx->d_straight_table);
    if (ctx->d_top_card_table) clReleaseMemObject(ctx->d_top_card_table);
    if (ctx->d_top_five_table) clReleaseMemObject(ctx->d_top_five_table);
    if (ctx->d_bottom_five_table) clReleaseMemObject(ctx->d_bottom_five_table);
    if (ctx->d_bottom_card_table) clReleaseMemObject(ctx->d_bottom_card_table);

    if (ctx->kernel_basic) clReleaseKernel(ctx->kernel_basic);
    if (ctx->kernel_opt) clReleaseKernel(ctx->kernel_opt);
    if (ctx->kernel_equity) clReleaseKernel(ctx->kernel_equity);
    if (ctx->kernel_omaha) clReleaseKernel(ctx->kernel_omaha);
    if (ctx->kernel_omaha5) clReleaseKernel(ctx->kernel_omaha5);
    if (ctx->kernel_omaha6) clReleaseKernel(ctx->kernel_omaha6);
    if (ctx->kernel_stud) clReleaseKernel(ctx->kernel_stud);
    if (ctx->kernel_razz) clReleaseKernel(ctx->kernel_razz);
    if (ctx->kernel_omaha_hilo) clReleaseKernel(ctx->kernel_omaha_hilo);
    if (ctx->kernel_hilo) clReleaseKernel(ctx->kernel_hilo);

    if (ctx->program) clReleaseProgram(ctx->program);
    if (ctx->queue) clReleaseCommandQueue(ctx->queue);
    if (ctx->context) clReleaseContext(ctx->context);

    free(ctx);
}

/**
 * Evaluate batch of Hold'em hands (OpenCL)
 */
int gpu_eval_holdem_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    /* Transfer hands to device */
    size_t hands_size = batch_size * 7 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Use basic kernel (has correct signature for lookup tables) */
    cl_kernel kernel = ctx->kernel_basic;

    /* Set kernel arguments including lookup tables */
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &batch_size);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_top_five_table);

    /* Configure work dimensions */
    size_t global_size = ((batch_size + 255) / 256) * 256; /* Round up to multiple of 256 */
    size_t local_size = 256;

    /* Launch kernel */
    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    /* Read results back */
    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;

    return 0;
}

/**
 * Evaluate batch of Omaha-4 hands (OpenCL)
 */
int gpu_eval_omaha_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_omaha) {
        fprintf(stderr, "Omaha kernel not available\n");
        return -1;
    }

    /* Transfer hands to device (9 cards: 4 hole + 5 board) */
    size_t hands_size = batch_size * 9 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Set kernel arguments including lookup tables */
    cl_kernel kernel = ctx->kernel_omaha;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &batch_size);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_top_five_table);

    /* Configure work dimensions */
    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    /* Launch kernel */
    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    /* Read results */
    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;

    return 0;
}

/**
 * Evaluate batch of Omaha-5 hands (OpenCL)
 */
int gpu_eval_omaha5_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_omaha5) {
        fprintf(stderr, "Omaha5 kernel not available\n");
        return -1;
    }

    /* Transfer hands to device (10 cards: 5 hole + 5 board) */
    size_t hands_size = batch_size * 10 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Set kernel arguments including lookup tables */
    cl_kernel kernel = ctx->kernel_omaha5;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &batch_size);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_top_five_table);

    /* Configure work dimensions */
    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    /* Launch kernel */
    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    /* Read results */
    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;

    return 0;
}

/**
 * Evaluate batch of Omaha-6 hands (OpenCL)
 */
int gpu_eval_omaha6_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_omaha6) {
        fprintf(stderr, "Omaha6 kernel not available\n");
        return -1;
    }

    /* Transfer hands to device (11 cards: 6 hole + 5 board) */
    size_t hands_size = batch_size * 11 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Set kernel arguments including lookup tables */
    cl_kernel kernel = ctx->kernel_omaha6;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &batch_size);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_top_five_table);

    /* Configure work dimensions */
    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    /* Launch kernel */
    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    /* Read results */
    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;

    return 0;
}

/* ===== Stud / Razz / Hi-Lo batch implementations ===== */

/**
 * Evaluate batch of 7-card Stud hands (OpenCL, high evaluation)
 * Same C(7,5)=21 combo logic as Hold'em — reuses stud kernel.
 */
int gpu_eval_stud_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_stud) {
        fprintf(stderr, "Stud kernel not available\n");
        return -1;
    }

    size_t hands_size = batch_size * 7 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_kernel kernel = ctx->kernel_stud;
    cl_ulong bs = (cl_ulong)batch_size;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &bs);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_top_five_table);

    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;
    return 0;
}

/**
 * Evaluate batch of 7-card Razz hands (OpenCL, A-5 low evaluation)
 */
int gpu_eval_razz_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_values
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_razz) {
        fprintf(stderr, "Razz kernel not available\n");
        return -1;
    }

    size_t hands_size = batch_size * 7 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_kernel kernel = ctx->kernel_razz;
    cl_ulong bs = (cl_ulong)batch_size;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &bs);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_bottom_five_table);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_bottom_card_table);

    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    size_t values_size = batch_size * sizeof(uint32_t);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_values, CL_TRUE,
                              0, values_size, out_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;
    return 0;
}

/**
 * Evaluate batch with Hi/Lo split (OpenCL, generic game type)
 * Uses eval_hilo_batch_kernel which enumerates all C(n,5) combinations.
 */
int gpu_eval_hilo_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    int cards_per_hand,
    int game_type,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    (void)game_type; /* Game type is implicit in cards_per_hand for generic C(n,5) eval */

    if (!ctx->kernel_hilo) {
        fprintf(stderr, "Generic Hi/Lo kernel not available\n");
        return -1;
    }

    size_t hands_size = batch_size * (size_t)cards_per_hand * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_kernel kernel = ctx->kernel_hilo;
    cl_ulong bs = (cl_ulong)batch_size;
    cl_int cph = (cl_int)cards_per_hand;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &bs);
    clSetKernelArg(kernel, 2, sizeof(cl_int), &cph);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_hi_values);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_lo_values);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_lo_qualifies);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &ctx->d_top_five_table);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &ctx->d_bottom_five_table);
    clSetKernelArg(kernel, 11, sizeof(cl_mem), &ctx->d_bottom_card_table);

    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    /* Read back hi, lo, and qualification results */
    size_t values_size = batch_size * sizeof(uint32_t);
    size_t qualifies_size = batch_size * sizeof(int);

    err = clEnqueueReadBuffer(ctx->queue, ctx->d_hi_values, CL_TRUE,
                              0, values_size, out_hi_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_lo_values, CL_TRUE,
                              0, values_size, out_lo_values, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_lo_qualifies, CL_TRUE,
                              0, qualifies_size, out_lo_qualifies, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;
    return 0;
}

/**
 * Evaluate batch of Omaha Hi/Lo hands (OpenCL, 4 hole + 5 board)
 */
int gpu_eval_omaha_hilo_batch_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t* hands,
    size_t batch_size,
    uint32_t* out_hi_values,
    uint32_t* out_lo_values,
    int* out_lo_qualifies
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    cl_int err;

    if (!ctx->kernel_omaha_hilo) {
        fprintf(stderr, "Omaha Hi/Lo kernel not available\n");
        return -1;
    }

    size_t hands_size = batch_size * 9 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_hands, CL_FALSE,
                               0, hands_size, hands, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_kernel kernel = ctx->kernel_omaha_hilo;
    cl_ulong bs = (cl_ulong)batch_size;

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &ctx->d_hands);
    clSetKernelArg(kernel, 1, sizeof(cl_ulong), &bs);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &ctx->d_hi_values);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &ctx->d_lo_values);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_lo_qualifies);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &ctx->d_top_five_table);
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &ctx->d_bottom_five_table);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &ctx->d_bottom_card_table);

    size_t global_size = ((batch_size + 255) / 256) * 256;
    size_t local_size = 256;

    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    size_t values_size = batch_size * sizeof(uint32_t);
    size_t qualifies_size = batch_size * sizeof(int);

    err = clEnqueueReadBuffer(ctx->queue, ctx->d_hi_values, CL_TRUE,
                              0, values_size, out_hi_values, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_lo_values, CL_TRUE,
                              0, values_size, out_lo_values, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_lo_qualifies, CL_TRUE,
                              0, qualifies_size, out_lo_qualifies, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;
    return 0;
}

int gpu_eval_equity_holdem_opencl(
    gpu_eval_context_t* ctx_opaque,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses
) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    if (!ctx || !hero_hand || !villain_hand || !boards ||
        !out_wins || !out_ties || !out_losses || num_boards == 0) {
        return -1;
    }

    if (!ctx->kernel_equity) {
        fprintf(stderr, "Equity kernel not available\n");
        return -1;
    }

    cl_int err;

    size_t boards_size = num_boards * 5 * sizeof(uint8_t);
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_boards, CL_FALSE,
                               0, boards_size, boards, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_ulong zero = 0;
    err = clEnqueueFillBuffer(ctx->queue, ctx->d_wins, &zero, sizeof(cl_ulong),
                              0, sizeof(cl_ulong), 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueFillBuffer(ctx->queue, ctx->d_ties, &zero, sizeof(cl_ulong),
                              0, sizeof(cl_ulong), 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueFillBuffer(ctx->queue, ctx->d_losses, &zero, sizeof(cl_ulong),
                              0, sizeof(cl_ulong), 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    cl_kernel kernel = ctx->kernel_equity;

    cl_uchar hero0 = hero_hand[0];
    cl_uchar hero1 = hero_hand[1];
    cl_uchar villain0 = villain_hand[0];
    cl_uchar villain1 = villain_hand[1];
    cl_ulong boards_count = (cl_ulong)num_boards;

    clSetKernelArg(kernel, 0, sizeof(cl_uchar), &hero0);
    clSetKernelArg(kernel, 1, sizeof(cl_uchar), &hero1);
    clSetKernelArg(kernel, 2, sizeof(cl_uchar), &villain0);
    clSetKernelArg(kernel, 3, sizeof(cl_uchar), &villain1);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &ctx->d_boards);
    clSetKernelArg(kernel, 5, sizeof(cl_ulong), &boards_count);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &ctx->d_wins);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &ctx->d_ties);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &ctx->d_losses);
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &ctx->d_nbits_table);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &ctx->d_straight_table);
    clSetKernelArg(kernel, 11, sizeof(cl_mem), &ctx->d_top_card_table);
    clSetKernelArg(kernel, 12, sizeof(cl_mem), &ctx->d_top_five_table);

    size_t global_size = ((num_boards + 255) / 256) * 256;
    size_t local_size = 256;

    cl_event event;
    err = clEnqueueNDRangeKernel(ctx->queue, kernel, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &event);
    CL_CHECK_ERR(err, -1);

    cl_ulong wins = 0, ties = 0, losses = 0;
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_wins, CL_TRUE,
                              0, sizeof(cl_ulong), &wins, 1, &event, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_ties, CL_TRUE,
                              0, sizeof(cl_ulong), &ties, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_losses, CL_TRUE,
                              0, sizeof(cl_ulong), &losses, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    ctx->last_event = event;

    *out_wins = wins;
    *out_ties = ties;
    *out_losses = losses;

    return 0;
}

int gpu_eval_get_device_count_opencl(void) {
    cl_uint num_platforms = 0;
    if (clGetPlatformIDs(0, NULL, &num_platforms) != CL_SUCCESS || num_platforms == 0) {
        return 0;
    }

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    if (!platforms) {
        return 0;
    }

    cl_int err = clGetPlatformIDs(num_platforms, platforms, NULL);
    if (err != CL_SUCCESS) {
        free(platforms);
        return 0;
    }

    int total = 0;
    for (cl_uint p = 0; p < num_platforms; ++p) {
        cl_uint count = 0;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 0, NULL, &count);
        if (err == CL_SUCCESS) {
            total += count;
        }
    }

    free(platforms);
    return total;
}

/**
 * Get last kernel execution time (OpenCL)
 */
double gpu_eval_get_last_time_opencl(gpu_eval_context_t* ctx_opaque) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;

    if (!ctx->last_event) return 0.0;

    cl_ulong start, end;
    clGetEventProfilingInfo(ctx->last_event, CL_PROFILING_COMMAND_START,
                           sizeof(cl_ulong), &start, NULL);
    clGetEventProfilingInfo(ctx->last_event, CL_PROFILING_COMMAND_END,
                           sizeof(cl_ulong), &end, NULL);

    /* Convert nanoseconds to milliseconds */
    return (double)(end - start) / 1000000.0;
}

/**
 * Get device info string (OpenCL)
 */
const char* gpu_eval_get_device_name_opencl(gpu_eval_context_t* ctx_opaque) {
    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;
    return ctx->device_name;
}

void gpu_eval_fill_device_info_opencl(gpu_eval_context_t* ctx_opaque, gpu_device_info_t* info) {
    if (!ctx_opaque || !info) {
        return;
    }

    gpu_eval_context_opencl_t* ctx = (gpu_eval_context_opencl_t*)ctx_opaque;

    info->backend = GPU_BACKEND_OPENCL;
    info->device_id = ctx->device_index;
    strncpy(info->device_name, ctx->device_name, sizeof(info->device_name) - 1);
    info->device_name[sizeof(info->device_name) - 1] = '\0';
    info->global_mem_size = (size_t)ctx->global_mem_size;
    info->shared_mem_per_block = (size_t)ctx->local_mem_size;
    info->max_threads_per_block = (int)ctx->max_work_group_size;
    info->warp_size = 0;
    info->compute_capability_major = -1;
    info->compute_capability_minor = -1;
}
