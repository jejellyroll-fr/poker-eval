/* Internal declaration for the GPU-06 OpenCL kernel source fragment. */

#ifndef POKER_EVAL_KERNELS_REGRET_OPENCL_H
#define POKER_EVAL_KERNELS_REGRET_OPENCL_H

#include <poker_eval/gpu/pe_regret_opencl.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *pe_regret_opencl_kernel_source(void);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_KERNELS_REGRET_OPENCL_H */
