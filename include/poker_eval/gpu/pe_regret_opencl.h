/* pe_regret_opencl.h - OpenCL ragged strategy kernel adapter (GPU-06). */

#ifndef POKER_EVAL_GPU_PE_REGRET_OPENCL_H
#define POKER_EVAL_GPU_PE_REGRET_OPENCL_H

#include <poker_eval/solver/pe_compute.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_regret_opencl_context_t pe_regret_opencl_context_t;

pe_regret_opencl_context_t *pe_regret_opencl_create(void);
void pe_regret_opencl_destroy(pe_regret_opencl_context_t *ctx);
int pe_regret_opencl_strategy_batch(pe_regret_opencl_context_t *ctx,
                                    const pe_infoset_batch_t *in,
                                    pe_strategy_batch_t *out);
int pe_regret_opencl_sync(pe_regret_opencl_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_GPU_PE_REGRET_OPENCL_H */
