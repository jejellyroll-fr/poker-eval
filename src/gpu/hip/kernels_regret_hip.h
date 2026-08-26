/* Internal launch ABI for the GPU-06 ragged HIP regret kernels. */

#ifndef POKER_EVAL_KERNELS_REGRET_HIP_H
#define POKER_EVAL_KERNELS_REGRET_HIP_H

#include <hip/hip_runtime.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pe_hip_launch_strategy_batch(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count, hipStream_t stream);

void pe_hip_launch_apply_update_batch(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count, hipStream_t stream);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_KERNELS_REGRET_HIP_H */
