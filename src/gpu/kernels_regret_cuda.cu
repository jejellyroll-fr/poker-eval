/* kernels_regret_cuda.cu - ragged strategy and regret-update primitives (GPU-06). */

#include <cuda_runtime.h>
#include <stdint.h>
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

}
