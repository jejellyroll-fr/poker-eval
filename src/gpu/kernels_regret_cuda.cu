/*
 * kernels_regret_cuda.cu - CUDA instance of the shared regret primitives.
 *
 * The kernels and the host-side transfer logic live in
 * common/pe_regret_kernels.inc; this file only says how the device runtime is
 * spelled and what the exported symbols are called. The HIP backend is the
 * same two declarations against a different runtime header.
 */

#include <stdint.h>
#include <math.h>
#include <new>
#include <poker_eval/gpu/pe_regret_cuda.h>
#include "kernels_regret_cuda.h"
#include "common/pe_gpu_runtime_cuda.h"

#define PE_GPU_FN(name)  pe_regret_cuda_##name
#define PE_GPU_KFN(name) pe_cuda_##name
#define PE_GPU_CTX       pe_regret_cuda_context_t
#define PE_GPU_BATCH     pe_regret_cuda_update_batch_t

#include "common/pe_regret_kernels.inc"
