/* Internal launch ABI for the GPU-06 ragged CUDA regret kernels. */

#ifndef POKER_EVAL_KERNELS_REGRET_CUDA_H
#define POKER_EVAL_KERNELS_REGRET_CUDA_H

#include <cuda_runtime.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pe_cuda_launch_strategy_batch(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count, cudaStream_t stream);

void pe_cuda_launch_apply_update_batch(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count, cudaStream_t stream);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_KERNELS_REGRET_CUDA_H */
