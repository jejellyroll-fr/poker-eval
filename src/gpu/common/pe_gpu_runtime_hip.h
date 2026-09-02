/* HIP spelling of the device runtime used by pe_regret_kernels.inc. */

#ifndef POKER_EVAL_PE_GPU_RUNTIME_HIP_H
#define POKER_EVAL_PE_GPU_RUNTIME_HIP_H

#include <hip/hip_runtime.h>

#define PE_GPU_GLOBAL              __global__
#define PE_GPU_ATOMIC_ADD(p, v)    atomicAdd((p), (v))
#define PE_GPU_LAUNCH(kernel, blocks, threads, stream, ...) \
    kernel<<<(blocks), (threads), 0, (stream)>>>(__VA_ARGS__)

typedef hipStream_t peGpuStream_t;
typedef hipError_t  peGpuError_t;

#define peGpuSuccess               hipSuccess
#define peGpuMemcpyHostToDevice    hipMemcpyHostToDevice
#define peGpuMemcpyDeviceToHost    hipMemcpyDeviceToHost
#define peGpuMalloc                hipMalloc
#define peGpuFree                  hipFree
#define peGpuMemcpyAsync           hipMemcpyAsync
#define peGpuMemsetAsync           hipMemsetAsync
#define peGpuStreamCreate          hipStreamCreate
#define peGpuStreamDestroy         hipStreamDestroy
#define peGpuStreamSynchronize     hipStreamSynchronize
#define peGpuGetLastError          hipGetLastError

#endif /* POKER_EVAL_PE_GPU_RUNTIME_HIP_H */
