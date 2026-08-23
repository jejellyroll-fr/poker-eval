/*
 * gpu_cfr_opencl.c - OpenCL backend for GPU-CFR
 *
 * Copyright (C) 2025 poker-eval contributors
 *
 * Implements GPU-accelerated CFR using OpenCL for cross-platform support
 * (AMD, Intel, NVIDIA GPUs). Uses matrix formulation with AXPY updates,
 * regret matching, and sparse matrix operations.
 *
 * OpenCL port of GPU-CFR CUDA implementation for broader hardware support.
 */

#include <poker_eval/gpu/gpu_cfr.h>
#include "gpu_cfr_opencl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kernels_regret_opencl.h"

#ifdef __APPLE__
/* <OpenCL/opencl.h> also pulls in cl_gl.h and, through it,
 * OpenGL/CGLDevice.h, which re-typedefs cl_device_id with an availability
 * attribute — GCC rejects that as a redefinition of typedef against cl.h's
 * plain one. Nothing here uses CL/GL interop, so include the core header
 * only, matching what every other platform gets. */
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

/* ===== OpenCL Context Structure ===== */

struct gpu_cfr_opencl_context_t {
    gpu_cfr_config_t config;

    /* OpenCL objects */
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;

    /* CFR Kernels */
    cl_kernel kernel_regret_matching;
    cl_kernel kernel_axpy;
    cl_kernel kernel_regret_update;
    cl_kernel kernel_strategy_accumulate;
    cl_kernel kernel_spmv_csr;
    cl_kernel kernel_zero;
    cl_kernel kernel_normalize;
    cl_kernel kernel_exploitability;
    cl_kernel kernel_sum_reduction;

    /* Device buffers for CFR matrices */
    cl_mem d_regrets;           /* [num_infosets × max_actions] */
    cl_mem d_avg_strategy;      /* [num_infosets × max_actions] */
    cl_mem d_curr_strategy;     /* [num_infosets × max_actions] */
    cl_mem d_action_counts;     /* [num_infosets] */
    cl_mem d_deltas;            /* [num_infosets × max_actions] for regret deltas */

    /* Sparse transition matrix (device) */
    cl_mem d_csr_row_ptr;
    cl_mem d_csr_col_idx;
    cl_mem d_csr_values;
    int csr_num_rows;
    int csr_nnz;

    /* Temporary buffers */
    cl_mem d_temp;              /* Temp buffer for intermediate results */
    cl_mem d_exploitability;    /* Per-infoset exploitability */
    cl_mem d_reduction_output;  /* Output for reductions */

    /* Host storage reference */
    cfr_matrix_storage_t* host_storage;

    /* Statistics */
    gpu_cfr_stats_t stats;

    /* Device properties */
    size_t max_work_group_size;
    cl_ulong global_mem_size;
    cl_ulong local_mem_size;
    int compute_units;
    char device_name[256];

    /* Profiling events */
    cl_event last_event;
    bool profiling_enabled;
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

/* ===== Embedded Kernel Source ===== */

/*
 * Kernel source, split into fragments. A single literal came to 4770 chars,
 * over the 4095 ISO C99 guarantees and rejected by -Woverlength-strings.
 * clCreateProgramWithSource concatenates the fragments in order, which is
 * exactly what its multi-string signature is for.
 */
static const char* GPU_CFR_KERNEL_SOURCE[] = {
"/* GPU-CFR OpenCL Kernels (embedded) */\n"
"\n"
"__kernel void regret_matching_kernel(\n"
"    __global const float* restrict regrets,\n"
"    __global float* restrict curr_strategy,\n"
"    __global const uchar* restrict action_counts,\n"
"    int num_infosets,\n"
"    int max_actions\n"
") {\n"
"    int infoset_id = get_global_id(0);\n"
"    if (infoset_id >= num_infosets) return;\n"
"    int n_actions = action_counts[infoset_id];\n"
"    if (n_actions == 0) return;\n"
"    int base_idx = infoset_id * max_actions;\n"
"    float sum_pos = 0.0f;\n"
"    for (int a = 0; a < n_actions; a++) {\n"
"        float r = regrets[base_idx + a];\n"
"        if (r > 0.0f) sum_pos += r;\n"
"    }\n"
"    if (sum_pos > 1e-9f) {\n"
"        for (int a = 0; a < n_actions; a++) {\n"
"            float r = regrets[base_idx + a];\n"
"            curr_strategy[base_idx + a] = (r > 0.0f) ? (r / sum_pos) : 0.0f;\n"
"        }\n"
"    } else {\n"
"        float uniform = 1.0f / (float)n_actions;\n"
"        for (int a = 0; a < n_actions; a++) {\n"
"            curr_strategy[base_idx + a] = uniform;\n"
"        }\n"
"    }\n"
"    for (int a = n_actions; a < max_actions; a++) {\n"
"        curr_strategy[base_idx + a] = 0.0f;\n"
"    }\n"
"}\n"
"\n"
"__kernel void axpy_kernel(\n"
"    float alpha,\n"
"    __global const float* restrict x,\n"
"    __global float* restrict y,\n"
"    int size\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= size) return;\n"
"    y[idx] = alpha * x[idx] + y[idx];\n"
"}\n"
"\n"
"__kernel void regret_update_kernel(\n"
"    float alpha,\n"
"    __global float* restrict regrets,\n"
"    __global const float* restrict deltas,\n"
"    int size\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= size) return;\n"
"    regrets[idx] = alpha * regrets[idx] + deltas[idx];\n"
"}\n"
"\n"
"__kernel void strategy_accumulate_kernel(\n"
"    float weight,\n"
"    __global float* restrict avg_strategy,\n"
"    __global const float* restrict curr_strategy,\n"
"    int size\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= size) return;\n"
"    avg_strategy[idx] += weight * curr_strategy[idx];\n"
"}\n"
"\n"
"__kernel void spmv_csr_kernel(\n"
"    __global const int* restrict row_ptr,\n"
"    __global const int* restrict col_idx,\n"
"    __global const float* restrict values,\n"
"    __global const float* restrict x,\n"
"    __global float* restrict y,\n"
"    int num_rows\n"
") {\n"
"    int row = get_global_id(0);\n"
"    if (row >= num_rows) return;\n"
"    float sum = 0.0f;\n"
"    int start = row_ptr[row];\n"
"    int end = row_ptr[row + 1];\n"
"    for (int k = start; k < end; k++) {\n"
"        int col = col_idx[k];\n"
"        sum += values[k] * x[col];\n"
"    }\n"
"    y[row] = sum;\n"
"}\n"
"\n"
"__kernel void zero_kernel(\n"
"    __global float* restrict data,\n"
"    int size\n"
") {\n"
"    int idx = get_global_id(0);\n"
"    if (idx >= size) return;\n"
"    data[idx] = 0.0f;\n"
"}\n"
"\n"
,
"__kernel void normalize_strategy_kernel(\n"
"    __global float* restrict strategy,\n"
"    __global const uchar* restrict action_counts,\n"
"    int num_infosets,\n"
"    int max_actions\n"
") {\n"
"    int infoset_id = get_global_id(0);\n"
"    if (infoset_id >= num_infosets) return;\n"
"    int n_actions = action_counts[infoset_id];\n"
"    if (n_actions == 0) return;\n"
"    int base_idx = infoset_id * max_actions;\n"
"    float sum = 0.0f;\n"
"    for (int a = 0; a < n_actions; a++) {\n"
"        sum += strategy[base_idx + a];\n"
"    }\n"
"    if (sum > 1e-9f) {\n"
"        for (int a = 0; a < n_actions; a++) {\n"
"            strategy[base_idx + a] /= sum;\n"
"        }\n"
"    } else {\n"
"        float uniform = 1.0f / (float)n_actions;\n"
"        for (int a = 0; a < n_actions; a++) {\n"
"            strategy[base_idx + a] = uniform;\n"
"        }\n"
"    }\n"
"}\n"
"\n"
"__kernel void exploitability_kernel(\n"
"    __global const float* restrict regrets,\n"
"    __global float* restrict exploitability_per_infoset,\n"
"    __global const uchar* restrict action_counts,\n"
"    int num_infosets,\n"
"    int max_actions\n"
") {\n"
"    int infoset_id = get_global_id(0);\n"
"    if (infoset_id >= num_infosets) return;\n"
"    int n_actions = action_counts[infoset_id];\n"
"    if (n_actions == 0) {\n"
"        exploitability_per_infoset[infoset_id] = 0.0f;\n"
"        return;\n"
"    }\n"
"    int base_idx = infoset_id * max_actions;\n"
"    float sum_pos = 0.0f;\n"
"    for (int a = 0; a < n_actions; a++) {\n"
"        float r = regrets[base_idx + a];\n"
"        if (r > 0.0f) sum_pos += r;\n"
"    }\n"
"    exploitability_per_infoset[infoset_id] = sum_pos;\n"
"}\n"
"\n"
"__kernel void sum_reduction_kernel(\n"
"    __global const float* restrict input,\n"
"    __global float* restrict output,\n"
"    __local float* scratch,\n"
"    int size\n"
") {\n"
"    int local_id = get_local_id(0);\n"
"    int global_id = get_global_id(0);\n"
"    int local_size = get_local_size(0);\n"
"    scratch[local_id] = (global_id < size) ? input[global_id] : 0.0f;\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    for (int stride = local_size / 2; stride > 0; stride >>= 1) {\n"
"        if (local_id < stride) {\n"
"            scratch[local_id] += scratch[local_id + stride];\n"
"        }\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"    if (local_id == 0) {\n"
"        output[get_group_id(0)] = scratch[0];\n"
"    }\n"
"}\n"
};

#define GPU_CFR_KERNEL_SOURCE_PARTS \
    (sizeof(GPU_CFR_KERNEL_SOURCE) / sizeof(GPU_CFR_KERNEL_SOURCE[0]))

/* ===== Initialization ===== */

gpu_cfr_opencl_context_t* gpu_cfr_init_opencl(const gpu_cfr_config_t* config) {
    if (!config) return NULL;

    cl_int err;
    const char *kernel_sources[GPU_CFR_KERNEL_SOURCE_PARTS + 1u];
    size_t source_index;

    for (source_index = 0u; source_index < GPU_CFR_KERNEL_SOURCE_PARTS;
         ++source_index)
        kernel_sources[source_index] = GPU_CFR_KERNEL_SOURCE[source_index];
    kernel_sources[GPU_CFR_KERNEL_SOURCE_PARTS] =
        pe_regret_opencl_kernel_source();

    /* Allocate context */
    gpu_cfr_opencl_context_t* ctx =
        (gpu_cfr_opencl_context_t*)calloc(1, sizeof(gpu_cfr_opencl_context_t));
    if (!ctx) return NULL;

    ctx->config = *config;
    ctx->profiling_enabled = config->enable_profiling;

    /* Get platforms */
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
        free(platforms);
        free(ctx);
        return NULL;
    }

    /* Find suitable device (prefer GPU) */
    cl_device_id device = NULL;
    cl_platform_id selected_platform = NULL;

    for (cl_uint p = 0; p < num_platforms && !device; p++) {
        cl_uint num_devices = 0;
        err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, NULL, &num_devices);

        if (err == CL_SUCCESS && num_devices > 0) {
            cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
            if (!devices) {
                free(platforms);
                free(ctx);
                return NULL;
            }
            err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, num_devices, devices, NULL);

            if (err == CL_SUCCESS) {
                int idx = (config->device_id >= 0 && config->device_id < (int)num_devices)
                          ? config->device_id : 0;
                device = devices[idx];
                selected_platform = platforms[p];
            }
            free(devices);
        }
    }

    /* Fallback to CPU if no GPU */
    if (!device) {
        for (cl_uint p = 0; p < num_platforms && !device; p++) {
            cl_uint num_devices = 0;
            err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_CPU, 0, NULL, &num_devices);

            if (err == CL_SUCCESS && num_devices > 0) {
                err = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_CPU, 1, &device, NULL);
                if (err == CL_SUCCESS) {
                    selected_platform = platforms[p];
                }
            }
        }
    }

    free(platforms);

    if (!device) {
        fprintf(stderr, "No suitable OpenCL device found\n");
        free(ctx);
        return NULL;
    }

    ctx->platform = selected_platform;
    ctx->device = device;

    /* Query device properties */
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(ctx->device_name), ctx->device_name, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(ctx->max_work_group_size), &ctx->max_work_group_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE,
                    sizeof(ctx->global_mem_size), &ctx->global_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_LOCAL_MEM_SIZE,
                    sizeof(ctx->local_mem_size), &ctx->local_mem_size, NULL);
    clGetDeviceInfo(device, CL_DEVICE_MAX_COMPUTE_UNITS,
                    sizeof(ctx->compute_units), &ctx->compute_units, NULL);

    if (config->verbose) {
        printf("GPU-CFR OpenCL Device: %s\n", ctx->device_name);
        printf("  Compute Units: %d\n", ctx->compute_units);
        printf("  Max Work Group: %zu\n", ctx->max_work_group_size);
        printf("  Global Memory: %.2f MB\n", (double)ctx->global_mem_size / (1024.0 * 1024.0));
        printf("  Local Memory: %.2f KB\n", (double)ctx->local_mem_size / 1024.0);
    }

    /* Create context */
    ctx->context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create OpenCL context (err=%d)\n", err);
        free(ctx);
        return NULL;
    }

    /* Create command queue with profiling */
    cl_command_queue_properties props = config->enable_profiling ? CL_QUEUE_PROFILING_ENABLE : 0;
    ctx->queue = clCreateCommandQueue(ctx->context, device, props, &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create command queue (err=%d)\n", err);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Load and compile kernels from embedded source. Passing NULL lengths tells
     * OpenCL each fragment is NUL-terminated; it concatenates them in order. */
    ctx->program = clCreateProgramWithSource(ctx->context,
                                             (cl_uint)(GPU_CFR_KERNEL_SOURCE_PARTS + 1u),
                                             kernel_sources, NULL, &err);

    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create program (err=%d)\n", err);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Build program */
    err = clBuildProgram(ctx->program, 1, &device, "-cl-fast-relaxed-math -cl-mad-enable", NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(ctx->program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        if (log) {
            clGetProgramBuildInfo(ctx->program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
            fprintf(stderr, "OpenCL build error:\n%s\n", log);
            free(log);
        }

        clReleaseProgram(ctx->program);
        clReleaseCommandQueue(ctx->queue);
        clReleaseContext(ctx->context);
        free(ctx);
        return NULL;
    }

    /* Create kernels */
    ctx->kernel_regret_matching = clCreateKernel(ctx->program, "regret_matching_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create regret_matching_kernel (err=%d)\n", err);
    }

    ctx->kernel_axpy = clCreateKernel(ctx->program, "axpy_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create axpy_kernel (err=%d)\n", err);
    }

    ctx->kernel_regret_update = clCreateKernel(ctx->program, "regret_update_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create regret_update_kernel (err=%d)\n", err);
    }

    ctx->kernel_strategy_accumulate = clCreateKernel(ctx->program, "strategy_accumulate_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create strategy_accumulate_kernel (err=%d)\n", err);
    }

    ctx->kernel_spmv_csr = clCreateKernel(ctx->program, "spmv_csr_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create spmv_csr_kernel (err=%d)\n", err);
    }

    ctx->kernel_zero = clCreateKernel(ctx->program, "zero_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create zero_kernel (err=%d)\n", err);
    }

    ctx->kernel_normalize = clCreateKernel(ctx->program, "normalize_strategy_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create normalize_strategy_kernel (err=%d)\n", err);
    }

    ctx->kernel_exploitability = clCreateKernel(ctx->program, "exploitability_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create exploitability_kernel (err=%d)\n", err);
    }

    ctx->kernel_sum_reduction = clCreateKernel(ctx->program, "sum_reduction_kernel", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create sum_reduction_kernel (err=%d)\n", err);
    }

    /* Allocate device buffers */
    size_t matrix_size = config->num_infosets * config->max_actions * sizeof(float);
    size_t counts_size = config->num_infosets * sizeof(cl_uchar);

    ctx->d_regrets = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE, matrix_size, NULL, &err);
    ctx->d_avg_strategy = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE, matrix_size, NULL, &err);
    ctx->d_curr_strategy = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE, matrix_size, NULL, &err);
    ctx->d_action_counts = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY, counts_size, NULL, &err);
    ctx->d_deltas = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE, matrix_size, NULL, &err);
    ctx->d_temp = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE, matrix_size, NULL, &err);
    ctx->d_exploitability = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE,
                                           config->num_infosets * sizeof(float), NULL, &err);

    /* Reduction output buffer */
    int num_groups = (config->num_infosets + 255) / 256;
    ctx->d_reduction_output = clCreateBuffer(ctx->context, CL_MEM_READ_WRITE,
                                             num_groups * sizeof(float), NULL, &err);

    /* Allocate host storage */
    ctx->host_storage = cfr_matrix_storage_create(config->num_infosets, config->max_actions);
    if (!ctx->host_storage) {
        fprintf(stderr, "Failed to allocate host storage\n");
        gpu_cfr_free_opencl(ctx);
        return NULL;
    }

    if (config->verbose) {
        printf("GPU-CFR OpenCL initialized:\n");
        printf("  Infosets: %d\n", config->num_infosets);
        printf("  Max actions: %d\n", config->max_actions);
        printf("  GPU Memory: %.2f MB\n",
               (double)(matrix_size * 5 + counts_size) / (1024.0 * 1024.0));
    }

    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    return ctx;
}

/* ===== Cleanup ===== */

void gpu_cfr_free_opencl(gpu_cfr_opencl_context_t* ctx) {
    if (!ctx) return;

    /* Release buffers */
    if (ctx->d_regrets) clReleaseMemObject(ctx->d_regrets);
    if (ctx->d_avg_strategy) clReleaseMemObject(ctx->d_avg_strategy);
    if (ctx->d_curr_strategy) clReleaseMemObject(ctx->d_curr_strategy);
    if (ctx->d_action_counts) clReleaseMemObject(ctx->d_action_counts);
    if (ctx->d_deltas) clReleaseMemObject(ctx->d_deltas);
    if (ctx->d_temp) clReleaseMemObject(ctx->d_temp);
    if (ctx->d_exploitability) clReleaseMemObject(ctx->d_exploitability);
    if (ctx->d_reduction_output) clReleaseMemObject(ctx->d_reduction_output);

    /* Sparse matrix buffers */
    if (ctx->d_csr_row_ptr) clReleaseMemObject(ctx->d_csr_row_ptr);
    if (ctx->d_csr_col_idx) clReleaseMemObject(ctx->d_csr_col_idx);
    if (ctx->d_csr_values) clReleaseMemObject(ctx->d_csr_values);

    /* Release kernels */
    if (ctx->kernel_regret_matching) clReleaseKernel(ctx->kernel_regret_matching);
    if (ctx->kernel_axpy) clReleaseKernel(ctx->kernel_axpy);
    if (ctx->kernel_regret_update) clReleaseKernel(ctx->kernel_regret_update);
    if (ctx->kernel_strategy_accumulate) clReleaseKernel(ctx->kernel_strategy_accumulate);
    if (ctx->kernel_spmv_csr) clReleaseKernel(ctx->kernel_spmv_csr);
    if (ctx->kernel_zero) clReleaseKernel(ctx->kernel_zero);
    if (ctx->kernel_normalize) clReleaseKernel(ctx->kernel_normalize);
    if (ctx->kernel_exploitability) clReleaseKernel(ctx->kernel_exploitability);
    if (ctx->kernel_sum_reduction) clReleaseKernel(ctx->kernel_sum_reduction);

    /* Release program and context */
    if (ctx->program) clReleaseProgram(ctx->program);
    if (ctx->queue) clReleaseCommandQueue(ctx->queue);
    if (ctx->context) clReleaseContext(ctx->context);

    /* Free host storage */
    cfr_matrix_storage_free(ctx->host_storage);

    free(ctx);
}

/* ===== State Management ===== */

int gpu_cfr_load_state_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const cfr_matrix_storage_t* storage
) {
    if (!ctx || !storage) return -1;

    cl_int err;
    size_t matrix_size = storage->num_infosets * storage->max_actions * sizeof(float);
    size_t counts_size = storage->num_infosets * sizeof(cl_uchar);

    /* Upload regrets */
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_regrets, CL_FALSE,
                               0, matrix_size, storage->regrets, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Upload average strategy */
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_avg_strategy, CL_FALSE,
                               0, matrix_size, storage->avg_strategy, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Upload current strategy */
    if (storage->curr_strategy) {
        err = clEnqueueWriteBuffer(ctx->queue, ctx->d_curr_strategy, CL_FALSE,
                                   0, matrix_size, storage->curr_strategy, 0, NULL, NULL);
        CL_CHECK_ERR(err, -1);
    }

    /* Upload action counts */
    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_action_counts, CL_FALSE,
                               0, counts_size, storage->action_counts, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Wait for uploads to complete */
    err = clFinish(ctx->queue);
    CL_CHECK_ERR(err, -1);

    /* Copy to host storage */
    memcpy(ctx->host_storage->regrets, storage->regrets, matrix_size);
    memcpy(ctx->host_storage->avg_strategy, storage->avg_strategy, matrix_size);
    memcpy(ctx->host_storage->action_counts, storage->action_counts, counts_size);

    return 0;
}

int gpu_cfr_download_state_opencl(
    gpu_cfr_opencl_context_t* ctx,
    cfr_matrix_storage_t* storage
) {
    if (!ctx || !storage) return -1;

    cl_int err;
    size_t matrix_size = ctx->config.num_infosets * ctx->config.max_actions * sizeof(float);

    /* Download regrets */
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_regrets, CL_FALSE,
                              0, matrix_size, storage->regrets, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Download average strategy */
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_avg_strategy, CL_FALSE,
                              0, matrix_size, storage->avg_strategy, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Download current strategy */
    if (storage->curr_strategy) {
        err = clEnqueueReadBuffer(ctx->queue, ctx->d_curr_strategy, CL_FALSE,
                                  0, matrix_size, storage->curr_strategy, 0, NULL, NULL);
        CL_CHECK_ERR(err, -1);
    }

    /* Wait for downloads */
    err = clFinish(ctx->queue);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/* ===== GPU Kernel Execution ===== */

/**
 * Execute regret matching on GPU
 * Computes: curr_strategy = regret_matching(regrets)
 */
static int gpu_cfr_regret_matching(gpu_cfr_opencl_context_t* ctx) {
    cl_int err;
    int num_infosets = ctx->config.num_infosets;
    int max_actions = ctx->config.max_actions;

    /* Set kernel arguments */
    clSetKernelArg(ctx->kernel_regret_matching, 0, sizeof(cl_mem), &ctx->d_regrets);
    clSetKernelArg(ctx->kernel_regret_matching, 1, sizeof(cl_mem), &ctx->d_curr_strategy);
    clSetKernelArg(ctx->kernel_regret_matching, 2, sizeof(cl_mem), &ctx->d_action_counts);
    clSetKernelArg(ctx->kernel_regret_matching, 3, sizeof(int), &num_infosets);
    clSetKernelArg(ctx->kernel_regret_matching, 4, sizeof(int), &max_actions);

    /* Launch kernel */
    size_t global_size = ((num_infosets + 255) / 256) * 256;
    size_t local_size = 256;

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_regret_matching, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &ctx->last_event);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/**
 * Execute regret update on GPU
 * Computes: regrets = alpha * regrets + deltas
 */
static int gpu_cfr_regret_update(gpu_cfr_opencl_context_t* ctx, float alpha) {
    cl_int err;
    int size = ctx->config.num_infosets * ctx->config.max_actions;

    /* Set kernel arguments */
    clSetKernelArg(ctx->kernel_regret_update, 0, sizeof(float), &alpha);
    clSetKernelArg(ctx->kernel_regret_update, 1, sizeof(cl_mem), &ctx->d_regrets);
    clSetKernelArg(ctx->kernel_regret_update, 2, sizeof(cl_mem), &ctx->d_deltas);
    clSetKernelArg(ctx->kernel_regret_update, 3, sizeof(int), &size);

    /* Launch kernel */
    size_t global_size = ((size + 255) / 256) * 256;
    size_t local_size = 256;

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_regret_update, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &ctx->last_event);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/**
 * Execute strategy accumulation on GPU
 * Computes: avg_strategy += weight * curr_strategy
 */
static int gpu_cfr_strategy_accumulate(gpu_cfr_opencl_context_t* ctx, float weight) {
    cl_int err;
    int size = ctx->config.num_infosets * ctx->config.max_actions;

    /* Set kernel arguments */
    clSetKernelArg(ctx->kernel_strategy_accumulate, 0, sizeof(float), &weight);
    clSetKernelArg(ctx->kernel_strategy_accumulate, 1, sizeof(cl_mem), &ctx->d_avg_strategy);
    clSetKernelArg(ctx->kernel_strategy_accumulate, 2, sizeof(cl_mem), &ctx->d_curr_strategy);
    clSetKernelArg(ctx->kernel_strategy_accumulate, 3, sizeof(int), &size);

    /* Launch kernel */
    size_t global_size = ((size + 255) / 256) * 256;
    size_t local_size = 256;

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_strategy_accumulate, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &ctx->last_event);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/**
 * Execute SpMV on GPU
 * Computes: y = A * x (CSR sparse matrix-vector product)
 */
static int gpu_cfr_spmv(gpu_cfr_opencl_context_t* ctx,
                        cl_mem d_x, cl_mem d_y) {
    if (!ctx->d_csr_row_ptr || ctx->csr_nnz == 0) {
        return 0; /* No sparse matrix loaded */
    }

    cl_int err;

    /* Set kernel arguments */
    clSetKernelArg(ctx->kernel_spmv_csr, 0, sizeof(cl_mem), &ctx->d_csr_row_ptr);
    clSetKernelArg(ctx->kernel_spmv_csr, 1, sizeof(cl_mem), &ctx->d_csr_col_idx);
    clSetKernelArg(ctx->kernel_spmv_csr, 2, sizeof(cl_mem), &ctx->d_csr_values);
    clSetKernelArg(ctx->kernel_spmv_csr, 3, sizeof(cl_mem), &d_x);
    clSetKernelArg(ctx->kernel_spmv_csr, 4, sizeof(cl_mem), &d_y);
    clSetKernelArg(ctx->kernel_spmv_csr, 5, sizeof(int), &ctx->csr_num_rows);

    /* Launch kernel */
    size_t global_size = ((ctx->csr_num_rows + 255) / 256) * 256;
    size_t local_size = 256;

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_spmv_csr, 1, NULL,
                                 &global_size, &local_size, 0, NULL, &ctx->last_event);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/**
 * Compute exploitability proxy on GPU
 * Returns sum of positive regrets across all infosets
 */
static double gpu_cfr_compute_exploitability(gpu_cfr_opencl_context_t* ctx) {
    cl_int err;
    int num_infosets = ctx->config.num_infosets;
    int max_actions = ctx->config.max_actions;

    /* Step 1: Compute per-infoset exploitability */
    clSetKernelArg(ctx->kernel_exploitability, 0, sizeof(cl_mem), &ctx->d_regrets);
    clSetKernelArg(ctx->kernel_exploitability, 1, sizeof(cl_mem), &ctx->d_exploitability);
    clSetKernelArg(ctx->kernel_exploitability, 2, sizeof(cl_mem), &ctx->d_action_counts);
    clSetKernelArg(ctx->kernel_exploitability, 3, sizeof(int), &num_infosets);
    clSetKernelArg(ctx->kernel_exploitability, 4, sizeof(int), &max_actions);

    size_t global_size = ((num_infosets + 255) / 256) * 256;
    size_t local_size = 256;

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_exploitability, 1, NULL,
                                 &global_size, &local_size, 0, NULL, NULL);
    if (err != CL_SUCCESS) return -1.0;

    /* Step 2: Sum reduction */
    int num_groups = (num_infosets + 255) / 256;

    clSetKernelArg(ctx->kernel_sum_reduction, 0, sizeof(cl_mem), &ctx->d_exploitability);
    clSetKernelArg(ctx->kernel_sum_reduction, 1, sizeof(cl_mem), &ctx->d_reduction_output);
    clSetKernelArg(ctx->kernel_sum_reduction, 2, local_size * sizeof(float), NULL);
    clSetKernelArg(ctx->kernel_sum_reduction, 3, sizeof(int), &num_infosets);

    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_sum_reduction, 1, NULL,
                                 &global_size, &local_size, 0, NULL, NULL);
    if (err != CL_SUCCESS) return -1.0;

    /* Read partial sums and finish on CPU */
    float* partial_sums = (float*)malloc(num_groups * sizeof(float));
    if (!partial_sums)
        return -1.0;
    err = clEnqueueReadBuffer(ctx->queue, ctx->d_reduction_output, CL_TRUE,
                              0, num_groups * sizeof(float), partial_sums, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        free(partial_sums);
        return -1.0;
    }

    double total = 0.0;
    for (int i = 0; i < num_groups; i++) {
        total += partial_sums[i];
    }

    free(partial_sums);
    return total;
}

/* ===== Main Solve Function ===== */

/*
 * Device-side timestamp in milliseconds, obtained by profiling a one-byte
 * write. Returns 0.0 if the event cannot be timed, so a failure degrades the
 * reported duration instead of the solve itself.
 */
static double gpu_cfr_timestamp_ms(gpu_cfr_opencl_context_t* ctx) {
    cl_event event = NULL;
    cl_uchar dummy = 0;
    cl_ulong ns = 0;

    if (clEnqueueWriteBuffer(ctx->queue, ctx->d_action_counts, CL_TRUE,
                             0, 1, &dummy, 0, NULL, &event) != CL_SUCCESS) {
        return 0.0;
    }

    if (clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                sizeof(cl_ulong), &ns, NULL) != CL_SUCCESS) {
        ns = 0;
    }

    clReleaseEvent(event);
    return (double)ns / 1000000.0;
}

int gpu_cfr_solve_opencl(gpu_cfr_opencl_context_t* ctx, int iterations) {
    if (!ctx || iterations <= 0) return -1;

    if (ctx->config.verbose) {
        printf("Running %d GPU-CFR iterations (OpenCL)...\n", iterations);
    }

    double total_start = ctx->profiling_enabled ? gpu_cfr_timestamp_ms(ctx) : 0.0;

    for (int iter = 0; iter < iterations; iter++) {
        /* Step 1: Regret matching to compute current strategy */
        if (gpu_cfr_regret_matching(ctx) < 0) return -1;

        /* Step 2: Strategy accumulation (for average strategy) */
        float weight = ctx->config.strategy_weight;
        if (gpu_cfr_strategy_accumulate(ctx, weight) < 0) return -1;

        /*
         * Step 3: Game tree traversal / regret delta computation
         *
         * Note: This is the CPU-bound part that requires game-specific logic.
         * The deltas need to be computed via game tree traversal or sampling.
         * For now, we skip this part - in practice, the adapter computes deltas.
         */

        /* Step 4: Regret update with discounting */
        float alpha = ctx->config.regret_discount;
        if (gpu_cfr_regret_update(ctx, alpha) < 0) return -1;

        /* Progress reporting */
        if (ctx->config.verbose && ((iter + 1) % 100 == 0 || iter == iterations - 1)) {
            double exploitability = gpu_cfr_compute_exploitability(ctx);
            printf("  Iteration %d/%d, exploitability proxy: %.6f\n",
                   iter + 1, iterations, exploitability);
        }
    }

    /* Wait for all operations to complete */
    clFinish(ctx->queue);

    ctx->stats.iterations_completed += iterations;

    /* The start timestamp above had no counterpart, so total_time_ms and
     * avg_iteration_time_ms were never filled in by this backend. */
    if (ctx->profiling_enabled) {
        ctx->stats.total_time_ms += gpu_cfr_timestamp_ms(ctx) - total_start;
        if (ctx->stats.iterations_completed > 0) {
            ctx->stats.avg_iteration_time_ms =
                ctx->stats.total_time_ms / ctx->stats.iterations_completed;
        }
    }

    /* Compute final exploitability */
    ctx->stats.exploitability = gpu_cfr_compute_exploitability(ctx);

    if (ctx->config.verbose) {
        printf("GPU-CFR completed: %d iterations, exploitability=%.6f\n",
               iterations, ctx->stats.exploitability);
    }

    return 0;
}

/* ===== Sparse Matrix Loading ===== */

int gpu_cfr_load_sparse_matrix_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const sparse_matrix_csr_t* matrix
) {
    if (!ctx || !matrix) return -1;

    cl_int err;

    /* Free existing sparse buffers */
    if (ctx->d_csr_row_ptr) clReleaseMemObject(ctx->d_csr_row_ptr);
    if (ctx->d_csr_col_idx) clReleaseMemObject(ctx->d_csr_col_idx);
    if (ctx->d_csr_values) clReleaseMemObject(ctx->d_csr_values);

    ctx->csr_num_rows = matrix->num_rows;
    ctx->csr_nnz = matrix->nnz;

    /* Allocate and upload CSR data */
    size_t row_ptr_size = (matrix->num_rows + 1) * sizeof(int);
    size_t col_idx_size = matrix->nnz * sizeof(int);
    size_t values_size = matrix->nnz * sizeof(float);

    ctx->d_csr_row_ptr = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        row_ptr_size, matrix->row_ptr, &err);
    CL_CHECK_ERR(err, -1);

    ctx->d_csr_col_idx = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        col_idx_size, matrix->col_idx, &err);
    CL_CHECK_ERR(err, -1);

    ctx->d_csr_values = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                       values_size, matrix->values, &err);
    CL_CHECK_ERR(err, -1);

    if (ctx->config.verbose) {
        printf("Loaded sparse matrix: %d rows, %d nnz (%.2f%% sparse)\n",
               matrix->num_rows, matrix->nnz,
               100.0 * (1.0 - (double)matrix->nnz /
                              ((double)matrix->num_rows * (double)matrix->num_cols)));
    }

    return 0;
}

/* ===== Delta Loading ===== */

int gpu_cfr_load_deltas_opencl(
    gpu_cfr_opencl_context_t* ctx,
    const float* deltas
) {
    if (!ctx || !deltas) return -1;

    cl_int err;
    size_t matrix_size = ctx->config.num_infosets * ctx->config.max_actions * sizeof(float);

    err = clEnqueueWriteBuffer(ctx->queue, ctx->d_deltas, CL_FALSE,
                               0, matrix_size, deltas, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    return 0;
}

/* ===== Statistics ===== */

int gpu_cfr_get_stats_opencl(
    const gpu_cfr_opencl_context_t* ctx,
    gpu_cfr_stats_t* stats
) {
    if (!ctx || !stats) return -1;

    *stats = ctx->stats;
    return 0;
}

/* ===== Reset ===== */

int gpu_cfr_reset_opencl(gpu_cfr_opencl_context_t* ctx) {
    if (!ctx) return -1;

    cl_int err;
    int size = ctx->config.num_infosets * ctx->config.max_actions;

    /* Set kernel arguments for zero kernel */
    clSetKernelArg(ctx->kernel_zero, 0, sizeof(cl_mem), &ctx->d_regrets);
    clSetKernelArg(ctx->kernel_zero, 1, sizeof(int), &size);

    size_t global_size = ((size + 255) / 256) * 256;
    size_t local_size = 256;

    /* Zero regrets */
    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_zero, 1, NULL,
                                 &global_size, &local_size, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Zero average strategy */
    clSetKernelArg(ctx->kernel_zero, 0, sizeof(cl_mem), &ctx->d_avg_strategy);
    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_zero, 1, NULL,
                                 &global_size, &local_size, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    /* Zero current strategy */
    clSetKernelArg(ctx->kernel_zero, 0, sizeof(cl_mem), &ctx->d_curr_strategy);
    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel_zero, 1, NULL,
                                 &global_size, &local_size, 0, NULL, NULL);
    CL_CHECK_ERR(err, -1);

    clFinish(ctx->queue);

    memset(&ctx->stats, 0, sizeof(gpu_cfr_stats_t));

    return 0;
}

/* ===== Device Info ===== */

const char* gpu_cfr_get_device_name_opencl(const gpu_cfr_opencl_context_t* ctx) {
    return ctx ? ctx->device_name : "Unknown";
}

int gpu_cfr_get_device_count_opencl(void) {
    cl_uint num_platforms = 0;
    if (clGetPlatformIDs(0, NULL, &num_platforms) != CL_SUCCESS || num_platforms == 0) {
        return 0;
    }

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    if (!platforms) return 0;

    if (clGetPlatformIDs(num_platforms, platforms, NULL) != CL_SUCCESS) {
        free(platforms);
        return 0;
    }

    int total = 0;
    for (cl_uint p = 0; p < num_platforms; p++) {
        cl_uint count = 0;
        if (clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, NULL, &count) == CL_SUCCESS) {
            total += count;
        }
    }

    free(platforms);
    return total;
}
