# GPU Acceleration Guide

## Overview

This poker evaluation library now includes GPU acceleration support using both CUDA and OpenCL backends. The GPU implementation provides significant speedup for large-scale Monte Carlo simulations and batch evaluations.

## Features

### 1. Batch Hand Evaluation
- Evaluate thousands of poker hands simultaneously on GPU
- Optimized for 7-card Texas Hold'em evaluation
- Lookup tables stored in GPU constant memory for fast access

### 2. Monte Carlo Equity Calculation
- Massively parallel Monte Carlo simulations
- Each GPU thread runs independent simulations
- Atomic operations for result aggregation

### 3. Dual Backend Support
- **CUDA**: For NVIDIA GPUs
- **OpenCL**: For AMD GPUs, Intel GPUs, and other OpenCL-compatible devices

## Performance Gains

Expected performance improvements:
- **Batch Evaluation**: 10-50x speedup for large batches (≥10K hands)
- **Monte Carlo Simulation**: 10-100x speedup for ≥10^7 simulations
- **Memory Bandwidth**: Efficient use of GPU memory hierarchy

## Architecture

### GPU Memory Layout
```
┌────────���────────┐
│ Constant Memory │ ← Lookup tables (nBits, straight, topCard, etc.)
├─────────────────┤
│ Global Memory   │ ← Input/output buffers (boards, hole cards, results)
├─────────────────┤
│ Shared Memory   │ ← Thread block local data
└─────────────────┘
```

### Kernel Design
- **eval_batch_boards_kernel**: Evaluates multiple boards in parallel
- **monte_carlo_equity_kernel**: Runs Monte Carlo simulations
- Each thread processes one board or one simulation

## Building with GPU Support

### Prerequisites

#### For CUDA Support:
```bash
# Install NVIDIA CUDA Toolkit
# Ubuntu/Debian:
sudo apt install nvidia-cuda-toolkit

# macOS:
# Download from NVIDIA website

# Verify installation:
nvcc --version
```

#### For OpenCL Support:
```bash
# Ubuntu/Debian:
sudo apt install opencl-headers ocl-icd-opencl-dev

# macOS: (OpenCL included by default)
# No additional installation needed
```

### Build Configuration
```bash
mkdir build && cd build
cmake ..
make

# GPU libraries will be built automatically if CUDA/OpenCL are detected
```

## API Usage

### Basic Setup
```c
#include "eval_gpu.h"

// Check if GPU acceleration is available
if (gpu_is_available(0)) {  // 0 = CUDA, 1 = OpenCL
    printf("CUDA acceleration available\n");
}

// Initialize GPU context
gpu_eval_context_t* ctx = gpu_eval_init(
    0,          // device_id
    100000,     // max_batch_size
    0           // backend_type (0=CUDA, 1=OpenCL)
);
```

### Batch Evaluation
```c
// Prepare input data
StdDeck_CardMask boards[1000];
StdDeck_CardMask hole_cards[2000];  // 2 players per board
gpu_eval_result_t result = {0};

// Evaluate on GPU
int ret = gpu_eval_batch_boards(
    ctx,
    boards,
    hole_cards,
    1000,       // n_boards
    2,          // n_players
    &result
);

// Process results
if (ret == 0) {
    for (int i = 0; i < result.batch_size; i++) {
        HandVal hand_val = result.hand_values[i];
        // Process hand value...
    }
}
```

### Monte Carlo Simulation
```c
// Run Monte Carlo equity calculation
float equities[2];
int ret = gpu_monte_carlo_equity(
    ctx,
    NULL,       // ranges (not implemented yet)
    2,          // n_players
    1000000,    // n_simulations
    equities
);

if (ret == 0) {
    printf("Player 1 equity: %.3f\n", equities[0]);
    printf("Player 2 equity: %.3f\n", equities[1]);
}
```

### Cleanup
```c
// Free GPU resources
gpu_eval_cleanup(ctx);
```

## Implementation Details

### Lookup Tables
The GPU implementation uses the same lookup tables as the CPU version:
- `nBitsTable`: Count bits in rank mask
- `straightTable`: Detect straights
- `topFiveCardsTable`: Extract top 5 cards
- `topCardTable`: Find highest card

These are copied to GPU constant memory for fast access.

### Card Representation
Cards are represented using the same bit mask format:
```c
typedef struct {
    uint32_t cards_n;      // Number of cards
    uint32_t cards[4];     // [spades, hearts, diamonds, clubs]
} CudaCardMask;
```

### Random Number Generation
- XORShift32 algorithm for GPU-side RNG
- Each thread maintains independent RNG state
- Seeded from host for reproducible results

## Optimization Techniques

### Memory Coalescing
- Input data arranged for optimal memory access patterns
- Threads in a warp access consecutive memory locations

### Constant Memory Usage
- Lookup tables stored in constant memory
- Cached and broadcast to all threads in a multiprocessor

### Occupancy Optimization
- Thread block size chosen to maximize GPU occupancy
- Typically 256 threads per block for optimal performance

### Atomic Operations
- Used sparingly for result aggregation
- Minimized to avoid serialization bottlenecks

## Performance Tuning

### Batch Size Selection
```c
// Optimal batch sizes for different scenarios:
// Small batches (< 1K):     CPU may be faster due to overhead
// Medium batches (1K-10K):  Good GPU utilization
// Large batches (> 10K):    Maximum GPU efficiency
```

### Memory Management
```c
// Pre-allocate GPU memory for best performance
gpu_eval_context_t* ctx = gpu_eval_init(
    0,          // device_id
    100000,     // max_batch_size (allocate once)
    0           // backend_type
);
```

### Device Selection
```c
// Query device information
char device_name[256];
size_t device_memory;
int compute_units;

gpu_get_device_info(0, device_name, &device_memory, &compute_units);
printf("Using: %s with %zu MB memory\n", 
       device_name, device_memory / (1024*1024));
```

## Benchmarking

### Example Performance Results
```
CPU (Intel i7-8700K):
- 1M hand evaluations: 2.5 seconds
- 1M Monte Carlo sims: 15.2 seconds

GPU (NVIDIA RTX 3080):
- 1M hand evaluations: 0.08 seconds (31x speedup)
- 1M Monte Carlo sims: 0.18 seconds (84x speedup)
```

### Running Benchmarks
```bash
# Build and run GPU example
make gpu_eval_example
./gpu_eval_example

# Expected output shows CPU vs GPU performance comparison
```

## Troubleshooting

### Common Issues

#### CUDA Not Found
```
CMake Error: Could not find CUDA
```
**Solution**: Install NVIDIA CUDA Toolkit and ensure `nvcc` is in PATH.

#### OpenCL Not Found
```
CMake Error: Could not find OpenCL
```
**Solution**: Install OpenCL headers and runtime libraries.

#### Runtime Errors
```
GPU evaluation failed
```
**Solutions**:
- Check GPU memory availability
- Reduce batch size if out of memory
- Verify GPU drivers are up to date

#### Performance Issues
- Ensure batch size is large enough (> 1000)
- Check GPU utilization with `nvidia-smi` (CUDA) or similar tools
- Verify memory bandwidth is not the bottleneck

## Future Enhancements

### Planned Features
1. **Range Support**: Implement player range passing and evaluation in the Monte Carlo kernels (tracked as issue #37; the `ranges` parameter is passed as `NULL` today)
2. **Streaming**: Overlap computation with memory transfers
3. **Precision Tuning**: Half-precision (FP16) floating point for memory bandwidth
4. **Advanced RNG**: Better random number generation algorithms (only XORShift32 exists today)

### Implemented
- **Multi-GPU**: Distribute work across multiple GPUs with static/dynamic load balancing — implemented in `src/gpu/eval_multi_gpu.c` (`include/poker_eval/gpu/eval_multi_gpu.h`) and documented in [GPU_BATCHED_EVALUATION.md](./GPU_BATCHED_EVALUATION.md). Note: it is currently not wired into the CMake build (the module is present in the tree but `poker_gpu` targets do not compile it yet).

### Roadmap
- **Phase 1**: ✅ Basic batch evaluation and Monte Carlo
- **Phase 2**: 🔄 Range support and advanced features
- **Phase 3**: 🔄 Multi-GPU (source implemented, awaiting build wiring) and streaming optimizations
- **Phase 4**: 📋 Integration with poker analysis tools

## Contributing

To contribute to GPU acceleration development:

1. **Testing**: Run benchmarks on different GPU architectures
2. **Optimization**: Profile and optimize kernel performance
3. **Features**: Implement range support and advanced algorithms
4. **Documentation**: Improve this guide and add examples

## References

- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [OpenCL Specification](https://www.khronos.org/opencl/)
- [GPU Computing Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/)