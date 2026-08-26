/*
 * kernels_regret_hip.hip.cpp - HIP instance of the shared regret primitives.
 *
 * Deliberately C-like: no classes, no templates of ours, no STL beyond the
 * nothrow new the shared file uses. It is C++ only because hipcc compiles C++,
 * and it shares every line of arithmetic with the CUDA backend so the two
 * cannot drift.
 */

#include <stdint.h>
#include <math.h>
#include <new>
#include <poker_eval/gpu/pe_regret_hip.h>
#include "kernels_regret_hip.h"
#include "../common/pe_gpu_runtime_hip.h"

#define PE_GPU_FN(name)  pe_regret_hip_##name
#define PE_GPU_KFN(name) pe_hip_##name
#define PE_GPU_CTX       pe_regret_hip_context_t
#define PE_GPU_BATCH     pe_regret_hip_update_batch_t

#include "../common/pe_regret_kernels.inc"
