/* CUDA spelling of the device runtime used by pe_regret_kernels.inc. */

#ifndef POKER_EVAL_PE_GPU_RUNTIME_CUDA_H
#define POKER_EVAL_PE_GPU_RUNTIME_CUDA_H

#include <cuda_runtime.h>

#define PE_GPU_GLOBAL              __global__
#define PE_GPU_ATOMIC_ADD(p, v)    atomicAdd((p), (v))
#define PE_GPU_LAUNCH(kernel, blocks, threads, stream, ...) \
    kernel<<<(blocks), (threads), 0, (stream)>>>(__VA_ARGS__)

typedef cudaStream_t peGpuStream_t;
typedef cudaError_t  peGpuError_t;

#define peGpuSuccess               cudaSuccess
#define peGpuMemcpyHostToDevice    cudaMemcpyHostToDevice
#define peGpuMemcpyDeviceToHost    cudaMemcpyDeviceToHost
#define peGpuMalloc                cudaMalloc
#define peGpuFree                  cudaFree
#define peGpuMemcpyAsync           cudaMemcpyAsync
#define peGpuMemsetAsync           cudaMemsetAsync
#define peGpuStreamCreate          cudaStreamCreate
#define peGpuStreamDestroy         cudaStreamDestroy
#define peGpuStreamSynchronize     cudaStreamSynchronize
#define peGpuGetLastError          cudaGetLastError

#endif /* POKER_EVAL_PE_GPU_RUNTIME_CUDA_H */
