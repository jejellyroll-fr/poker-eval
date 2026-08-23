/* kernels_regret_cuda.cu - ragged strategy and regret-update primitives (GPU-06). */

#include <cuda_runtime.h>
#include <stdint.h>
#include <math.h>
#include <new>
#include <poker_eval/gpu/pe_regret_cuda.h>
#include "kernels_regret_cuda.h"

/* `offsets` has infoset_count + 1 entries. Each infoset owns the ragged span
 * [offsets[i], offsets[i + 1]) in the regret and strategy arrays. */
__global__ static void pe_strategy_batch_kernel(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count)
{
    uint32_t infoset = blockIdx.x * blockDim.x + threadIdx.x;
    if (infoset >= infoset_count)
        return;

    uint32_t begin = offsets[infoset];
    uint32_t end = offsets[infoset + 1u];
    uint16_t actions = action_counts[infoset];
    float positive = 0.0f;
    if (actions == 0u || begin + (uint32_t)actions > end)
        return;

    for (uint16_t action = 0u; action < actions; ++action)
    {
        float regret = regrets[begin + action];
        if (regret > 0.0f)
            positive += regret;
    }
    for (uint16_t action = 0u; action < actions; ++action)
    {
        float regret = regrets[begin + action];
        strategies[begin + action] = positive > 0.0f
            ? (regret > 0.0f ? regret / positive : 0.0f)
            : 1.0f / (float)actions;
    }
    for (uint32_t slot = begin + actions; slot < end; ++slot)
        strategies[slot] = 0.0f;
}

/* Updates are expected to be reduced by logical slot before launch. Atomic
 * adds keep the primitive safe for callers that submit duplicate slots. */
__global__ static void pe_apply_update_batch_kernel(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count)
{
    uint32_t update = blockIdx.x * blockDim.x + threadIdx.x;
    if (update >= update_count)
        return;
    uint32_t slot = slots[update];
    atomicAdd(&regrets[slot], regret_deltas[update]);
    atomicAdd(&averages[slot], average_deltas[update]);
}

struct pe_regret_cuda_context_t
{
    cudaStream_t stream;
};

extern "C" {

void pe_cuda_launch_strategy_batch(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count, cudaStream_t stream)
{
    const int threads = 256;
    if (infoset_count == 0u)
        return;
    const int blocks = (int)((infoset_count + (uint32_t)threads - 1u) /
                             (uint32_t)threads);
    pe_strategy_batch_kernel<<<blocks, threads, 0, stream>>>(
        regrets, offsets, action_counts, strategies, infoset_count);
}

void pe_cuda_launch_apply_update_batch(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count, cudaStream_t stream)
{
    const int threads = 256;
    if (update_count == 0u)
        return;
    const int blocks = (int)((update_count + (uint32_t)threads - 1u) /
                             (uint32_t)threads);
    pe_apply_update_batch_kernel<<<blocks, threads, 0, stream>>>(
        regrets, averages, slots, regret_deltas, average_deltas, update_count);
}

pe_regret_cuda_context_t *pe_regret_cuda_create(void)
{
    pe_regret_cuda_context_t *ctx = new (std::nothrow) pe_regret_cuda_context_t;
    if (ctx == nullptr || cudaStreamCreate(&ctx->stream) != cudaSuccess)
    {
        delete ctx;
        return nullptr;
    }
    return ctx;
}

void pe_regret_cuda_destroy(pe_regret_cuda_context_t *ctx)
{
    if (ctx == nullptr)
        return;
    cudaStreamDestroy(ctx->stream);
    delete ctx;
}

int pe_regret_cuda_strategy_batch(pe_regret_cuda_context_t *ctx,
                                  const pe_infoset_batch_t *in,
                                  pe_strategy_batch_t *out)
{
    float *device_regrets = nullptr;
    float *device_strategies = nullptr;
    uint32_t *device_offsets = nullptr;
    uint16_t *device_actions = nullptr;
    size_t total;
    cudaError_t status;

    if (ctx == nullptr || in == nullptr || out == nullptr ||
        (in->count != 0u &&
         (in->offsets == nullptr || in->action_counts == nullptr ||
          in->regrets == nullptr)) ||
        (out->capacity != 0u && out->strategies == nullptr))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    total = in->offsets[in->count];
    if (out->capacity < total || out->strategies == nullptr)
        return -1;
    for (size_t i = 0u; i < in->count; ++i)
    {
        uint32_t begin = in->offsets[i];
        uint32_t end = in->offsets[i + 1u];
        if (end < begin || (uint32_t)in->action_counts[i] > end - begin)
            return -1;
        for (uint16_t action = 0u; action < in->action_counts[i]; ++action)
            if (!isfinite(in->regrets[begin + action]))
                return -1;
    }
    status = cudaMalloc((void **)&device_regrets, total * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_strategies, total * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_offsets,
                        (in->count + 1u) * sizeof(uint32_t));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_actions,
                        in->count * sizeof(uint16_t));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_regrets, in->regrets,
                             total * sizeof(float), cudaMemcpyHostToDevice,
                             ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_offsets, in->offsets,
                             (in->count + 1u) * sizeof(uint32_t),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_actions, in->action_counts,
                             in->count * sizeof(uint16_t),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemsetAsync(device_strategies, 0, total * sizeof(float),
                             ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    pe_cuda_launch_strategy_batch(device_regrets, device_offsets,
                                  device_actions, device_strategies,
                                  (uint32_t)in->count, ctx->stream);
    status = cudaGetLastError();
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(out->strategies, device_strategies,
                             total * sizeof(float), cudaMemcpyDeviceToHost,
                             ctx->stream);
    if (status != cudaSuccess || cudaStreamSynchronize(ctx->stream) != cudaSuccess)
        goto fail;
    out->count = in->count;
    out->offsets = in->offsets;
    cudaFree(device_actions);
    cudaFree(device_offsets);
    cudaFree(device_strategies);
    cudaFree(device_regrets);
    return 0;
fail:
    cudaFree(device_actions);
    cudaFree(device_offsets);
    cudaFree(device_strategies);
    cudaFree(device_regrets);
    return -1;
}

int pe_regret_cuda_apply_update_slots(
    pe_regret_cuda_context_t *ctx, float *regrets, float *averages,
    size_t value_count, const pe_regret_cuda_update_batch_t *batch)
{
    float *device_regrets = nullptr;
    float *device_averages = nullptr;
    uint32_t *device_slots = nullptr;
    float *device_regret_deltas = nullptr;
    float *device_average_deltas = nullptr;
    cudaError_t status;

    if (ctx == nullptr || regrets == nullptr || averages == nullptr ||
        batch == nullptr || (batch->count != 0u &&
                             (batch->slots == nullptr ||
                              batch->regret_deltas == nullptr ||
                              batch->average_deltas == nullptr)) ||
        batch->count > UINT32_MAX)
        return -1;
    for (size_t i = 0u; i < batch->count; ++i)
    {
        uint32_t slot = batch->slots[i];
        if (slot >= value_count || !isfinite(regrets[slot]) ||
            !isfinite(averages[slot]) ||
            !isfinite(batch->regret_deltas[i]) ||
            !isfinite(batch->average_deltas[i]))
            return -1;
    }
    if (batch->count == 0u)
        return 0;
    status = cudaMalloc((void **)&device_regrets, value_count * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_averages, value_count * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_slots,
                        batch->count * sizeof(uint32_t));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_regret_deltas,
                        batch->count * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMalloc((void **)&device_average_deltas,
                        batch->count * sizeof(float));
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_regrets, regrets,
                             value_count * sizeof(float),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_averages, averages,
                             value_count * sizeof(float),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_slots, batch->slots,
                             batch->count * sizeof(uint32_t),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_regret_deltas, batch->regret_deltas,
                             batch->count * sizeof(float),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(device_average_deltas, batch->average_deltas,
                             batch->count * sizeof(float),
                             cudaMemcpyHostToDevice, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    pe_cuda_launch_apply_update_batch(
        device_regrets, device_averages, device_slots, device_regret_deltas,
        device_average_deltas, (uint32_t)batch->count, ctx->stream);
    status = cudaGetLastError();
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(regrets, device_regrets,
                             value_count * sizeof(float),
                             cudaMemcpyDeviceToHost, ctx->stream);
    if (status != cudaSuccess)
        goto fail;
    status = cudaMemcpyAsync(averages, device_averages,
                             value_count * sizeof(float),
                             cudaMemcpyDeviceToHost, ctx->stream);
    if (status != cudaSuccess || cudaStreamSynchronize(ctx->stream) != cudaSuccess)
        goto fail;
    cudaFree(device_average_deltas);
    cudaFree(device_regret_deltas);
    cudaFree(device_slots);
    cudaFree(device_averages);
    cudaFree(device_regrets);
    return 0;
fail:
    cudaFree(device_average_deltas);
    cudaFree(device_regret_deltas);
    cudaFree(device_slots);
    cudaFree(device_averages);
    cudaFree(device_regrets);
    return -1;
}

int pe_regret_cuda_sync(pe_regret_cuda_context_t *ctx)
{
    return ctx == nullptr || cudaStreamSynchronize(ctx->stream) != cudaSuccess
               ? -1
               : 0;
}

}
