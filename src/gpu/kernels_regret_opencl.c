/* kernels_regret_opencl.c - OpenCL ragged regret primitives (GPU-06). */

#include "kernels_regret_opencl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

struct pe_regret_opencl_context_t
{
    cl_context context;
    cl_device_id device;
    cl_command_queue queue;
    cl_program program;
    cl_kernel strategy_kernel;
};

/* Kept as a separately linkable source fragment so the OpenCL adapter can
 * append it to its existing program without duplicating the kernel text. */
static const char pe_regret_opencl_source[] =
"__kernel void pe_strategy_batch_kernel(\n"
"    __global const float *regrets, __global const uint *offsets,\n"
"    __global const ushort *action_counts, __global float *strategies,\n"
"    uint infoset_count)\n"
"{\n"
"    uint infoset = get_global_id(0);\n"
"    if (infoset >= infoset_count) return;\n"
"    uint begin = offsets[infoset];\n"
"    uint end = offsets[infoset + 1];\n"
"    ushort actions = action_counts[infoset];\n"
"    if (actions == 0 || begin + (uint)actions > end) return;\n"
"    float positive = 0.0f;\n"
"    for (ushort action = 0; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        if (regret > 0.0f) positive += regret;\n"
"    }\n"
"    for (ushort action = 0; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        strategies[begin + action] = positive > 0.0f\n"
"            ? (regret > 0.0f ? regret / positive : 0.0f)\n"
"            : 1.0f / (float)actions;\n"
"    }\n"
"    for (uint slot = begin + actions; slot < end; ++slot)\n"
"        strategies[slot] = 0.0f;\n"
"}\n"
"\n"
"__kernel void pe_apply_update_batch_kernel(\n"
"    __global float *regrets, __global float *averages,\n"
"    __global const uint *slots, __global const float *regret_deltas,\n"
"    __global const float *average_deltas, uint update_count)\n"
"{\n"
"    uint update = get_global_id(0);\n"
"    if (update >= update_count) return;\n"
"    uint slot = slots[update];\n"
"    /* Updates are reduced by slot before launch; one work item owns each\n"
"       reduced slot, so float atomics are unnecessary here. */\n"
"    regrets[slot] += regret_deltas[update];\n"
"    averages[slot] += average_deltas[update];\n"
"}\n";

const char *pe_regret_opencl_kernel_source(void)
{
    return pe_regret_opencl_source;
}

static cl_device_id pe_regret_opencl_device(void)
{
    cl_uint platform_count = 0u;
    cl_platform_id *platforms = NULL;
    cl_device_id device = NULL;

    if (clGetPlatformIDs(0u, NULL, &platform_count) != CL_SUCCESS ||
        platform_count == 0u)
        return NULL;
    platforms = (cl_platform_id *)calloc(platform_count, sizeof(*platforms));
    if (!platforms || clGetPlatformIDs(platform_count, platforms, NULL) !=
                       CL_SUCCESS)
    {
        free(platforms);
        return NULL;
    }
    for (cl_uint i = 0u; i < platform_count && !device; ++i)
        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 1u, &device,
                           NULL) != CL_SUCCESS)
            device = NULL;
    for (cl_uint i = 0u; i < platform_count && !device; ++i)
        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, 1u, &device,
                           NULL) != CL_SUCCESS)
            device = NULL;
    free(platforms);
    return device;
}

pe_regret_opencl_context_t *pe_regret_opencl_create(void)
{
    pe_regret_opencl_context_t *ctx;
    cl_int status;
    cl_device_id device = pe_regret_opencl_device();
    if (!device)
        return NULL;
    ctx = (pe_regret_opencl_context_t *)calloc(1u, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->device = device;
    ctx->context = clCreateContext(NULL, 1u, &ctx->device, NULL, NULL, &status);
    if (status != CL_SUCCESS)
        goto fail;
    ctx->queue = clCreateCommandQueue(ctx->context, ctx->device, 0u, &status);
    if (status != CL_SUCCESS)
        goto fail;
    {
        const char *source = pe_regret_opencl_kernel_source();
        ctx->program = clCreateProgramWithSource(ctx->context, 1u, &source,
                                                 NULL, &status);
    }
    if (status != CL_SUCCESS)
        goto fail;
    status = clBuildProgram(ctx->program, 1u, &ctx->device,
                            "-cl-fast-relaxed-math", NULL, NULL);
    if (status != CL_SUCCESS)
        goto fail;
    ctx->strategy_kernel = clCreateKernel(ctx->program,
                                          "pe_strategy_batch_kernel", &status);
    if (status != CL_SUCCESS)
        goto fail;
    return ctx;
fail:
    pe_regret_opencl_destroy(ctx);
    return NULL;
}

void pe_regret_opencl_destroy(pe_regret_opencl_context_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->strategy_kernel)
        clReleaseKernel(ctx->strategy_kernel);
    if (ctx->program)
        clReleaseProgram(ctx->program);
    if (ctx->queue)
        clReleaseCommandQueue(ctx->queue);
    if (ctx->context)
        clReleaseContext(ctx->context);
    free(ctx);
}

int pe_regret_opencl_strategy_batch(pe_regret_opencl_context_t *ctx,
                                    const pe_infoset_batch_t *in,
                                    pe_strategy_batch_t *out)
{
    cl_mem regrets = NULL;
    cl_mem offsets = NULL;
    cl_mem actions = NULL;
    cl_mem strategies = NULL;
    cl_int status;
    size_t total;

    if (!ctx || !in || !out || (in->count != 0u &&
        (!in->offsets || !in->action_counts || !in->regrets)) ||
        (out->capacity != 0u && !out->strategies) ||
        out->capacity < (in->count != 0u ? in->offsets[in->count] : 0u))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    if (!out->strategies)
        return -1;
    if (!out->offsets)
        out->offsets = in->offsets;
    total = in->offsets[in->count];
    for (size_t i = 0u; i < in->count; ++i)
    {
        if (in->offsets[i + 1u] < in->offsets[i] ||
            (uint32_t)in->action_counts[i] >
                in->offsets[i + 1u] - in->offsets[i])
            return -1;
        for (uint16_t a = 0u; a < in->action_counts[i]; ++a)
            if (!isfinite(in->regrets[in->offsets[i] + a]))
                return -1;
    }
    regrets = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY,
                             total * sizeof(float), NULL, &status);
    if (status != CL_SUCCESS)
        goto fail;
    offsets = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY,
                             (in->count + 1u) * sizeof(uint32_t),
                             NULL, &status);
    if (status != CL_SUCCESS)
        goto fail;
    actions = clCreateBuffer(ctx->context, CL_MEM_READ_ONLY,
                             in->count * sizeof(uint16_t),
                             NULL, &status);
    if (status != CL_SUCCESS)
        goto fail;
    status = clEnqueueWriteBuffer(ctx->queue, regrets, CL_TRUE, 0u,
                                  total * sizeof(float), in->regrets,
                                  0u, NULL, NULL);
    status |= clEnqueueWriteBuffer(ctx->queue, offsets, CL_TRUE, 0u,
                                   (in->count + 1u) * sizeof(uint32_t),
                                   in->offsets, 0u, NULL, NULL);
    status |= clEnqueueWriteBuffer(ctx->queue, actions, CL_TRUE, 0u,
                                   in->count * sizeof(uint16_t),
                                   in->action_counts, 0u, NULL, NULL);
    if (status != CL_SUCCESS)
        goto fail;
    strategies = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY,
                                total * sizeof(float), NULL, &status);
    if (status != CL_SUCCESS)
        goto fail;
    status = clSetKernelArg(ctx->strategy_kernel, 0u, sizeof(regrets), &regrets);
    status |= clSetKernelArg(ctx->strategy_kernel, 1u, sizeof(offsets), &offsets);
    status |= clSetKernelArg(ctx->strategy_kernel, 2u, sizeof(actions), &actions);
    status |= clSetKernelArg(ctx->strategy_kernel, 3u, sizeof(strategies),
                             &strategies);
    status |= clSetKernelArg(ctx->strategy_kernel, 4u, sizeof(cl_uint),
                             (cl_uint[]){(cl_uint)in->count});
    if (status != CL_SUCCESS)
        goto fail;
    {
        size_t global = in->count;
        status = clEnqueueNDRangeKernel(ctx->queue, ctx->strategy_kernel, 1u,
                                        NULL, &global, NULL, 0u, NULL, NULL);
    }
    if (status != CL_SUCCESS || clFinish(ctx->queue) != CL_SUCCESS)
        goto fail;
    status = clEnqueueReadBuffer(ctx->queue, strategies, CL_TRUE, 0u,
                                 total * sizeof(float), out->strategies,
                                 0u, NULL, NULL);
    clReleaseMemObject(strategies);
    clReleaseMemObject(actions);
    clReleaseMemObject(offsets);
    clReleaseMemObject(regrets);
    if (status != CL_SUCCESS)
        return -1;
    out->count = in->count;
    return 0;
fail:
    if (strategies) clReleaseMemObject(strategies);
    if (actions) clReleaseMemObject(actions);
    if (offsets) clReleaseMemObject(offsets);
    if (regrets) clReleaseMemObject(regrets);
    return -1;
}

int pe_regret_opencl_sync(pe_regret_opencl_context_t *ctx)
{
    return ctx && clFinish(ctx->queue) == CL_SUCCESS ? 0 : -1;
}
