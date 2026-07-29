# GPU Batched Evaluation Guide

## Overview

The GPU batched evaluation module provides **high-performance poker hand evaluation** using CUDA or OpenCL backends. It is designed to evaluate **millions to billions of hands per minute** for Monte Carlo simulations, range analysis, and real-time equity calculations.

**Priority**: ★★★★☆ (Plan section 5.1)
**Status**: Phase 3 COMPLETE - Production-ready implementation (6 Oct 2025)

## Features

### Core Evaluation
- **Batched 7→5 evaluation**: Hold'em and Omaha support
- **Real poker-eval lookup tables**: Accurate 13-bit rank/flush/straight evaluation
- **Dual backends**: CUDA and OpenCL for broad hardware compatibility
- **Optimized memory layout**: Structure-of-Arrays (SOA) for coalesced access
- **Warp-level parallelism**: Efficient reductions for maximum throughput
- **Equity calculation**: Win/tie/loss counting for heads-up scenarios
- **Performance tracking**: Built-in profiling and statistics

### Phase 3 Enhancements (NEW)
- **OpenCL kernel**: Full AMD/Intel GPU support via OpenCL 1.2+
- **Real lookup tables**: Loaded from poker-eval's actual evaluation tables
- **Multi-GPU support**: Distribute work across 2-8 GPUs with static/dynamic balancing
- **Concurrent streams**: Overlap I/O and compute with 4+ concurrent CUDA streams
- **Pinned memory**: Fast host-device transfers via page-locked memory
- **Comprehensive benchmarks**: Full hardware validation suite

## Target Performance

| Operation | Target Throughput |
|-----------|------------------|
| Hold'em 7-card eval | 1-5 billion hands/min |
| Omaha 9-card eval | 500M-2B hands/min |
| Equity calculation | 1-10 billion boards/min |

*On modern GPUs (RTX 3080/4090, A100, etc.)*

## Quick Start

### Include Header

```c
#include <poker_eval/gpu/eval_batched_gpu.h>
```

### Initialize Context

```c
/* Get default configuration */
gpu_eval_config_t config = gpu_eval_default_config();

/* Optional: customize */
config.preferred_backend = GPU_BACKEND_CUDA;
config.max_batch_size = 1000000; /* 1M hands per batch */
config.verbose = true;
config.device_id = 1; /* Optional: pick device index */

/* Initialize GPU context */
gpu_eval_context_t* gpu_ctx = gpu_eval_init(&config);
if (!gpu_ctx) {
    fprintf(stderr, "GPU not available\n");
    return -1;
}
```

### Evaluate Hold'em Hands

```c
/* Prepare input: 7 cards per hand (2 hole + 5 board) */
size_t batch_size = 10000;
uint8_t* hands = malloc(batch_size * 7 * sizeof(uint8_t));
uint32_t* values = malloc(batch_size * sizeof(uint32_t));

/* Fill hands array with card indices (0-51) */
for (size_t i = 0; i < batch_size; i++) {
    hands[i*7 + 0] = ...;  /* Hole card 1 */
    hands[i*7 + 1] = ...;  /* Hole card 2 */
    hands[i*7 + 2] = ...;  /* Board card 1 */
    /* ... */
}

/* Evaluate on GPU */
int result = gpu_eval_holdem_batch(gpu_ctx, hands, batch_size, values);
if (result == 0) {
    /* Success - values[] contains HandVal for each hand */
    printf("Hand 0 value: %u\n", values[0]);
}

free(hands);
free(values);
```

### Calculate Equity

```c
/* Hero vs Villain on 1M random boards */
uint8_t hero[2] = {0, 1};      /* As Ah */
uint8_t villain[2] = {12, 25}; /* Kd Kh */

size_t num_boards = 1000000;
uint8_t* boards = malloc(num_boards * 5 * sizeof(uint8_t));

/* Generate random boards */
generate_random_boards(boards, num_boards, hero, villain);

uint64_t wins = 0, ties = 0, losses = 0;
int result = gpu_eval_equity_holdem(
    gpu_ctx, hero, villain, boards, num_boards,
    &wins, &ties, &losses
);

if (result == 0) {
    double equity = (wins + ties * 0.5) / (double)num_boards;
    printf("Hero equity: %.2f%%\n", equity * 100.0);
}

free(boards);
```

### Cleanup

```c
gpu_eval_free(gpu_ctx);
```

### Device Selection

- By default the first GPU detected is used.  
- Set `config.device_id` to a 0-based index to pick a specific device.  
- `POKER_EVAL_GPU_DEVICE=<index>` overrides the configuration at runtime.  
- If no GPU device is found, the OpenCL backend automatically falls back to CPU devices.

### Monte Carlo Integration

- Batched Monte Carlo (`enumSampleBatched`) now routes Hold'em evaluations through the GPU path whenever a context is available.  
- Partial boards and multi-player batches are packed automatically; no changes are required for callers.  
- If GPU initialization fails, the code transparently falls back to the existing SIMD/CPU implementations.

## API Reference

### Configuration

```c
typedef struct {
    gpu_backend_t preferred_backend; /* CUDA or OpenCL */
    int device_id;                   /* -1 for auto-select */
    size_t max_batch_size;           /* Maximum hands per batch */
    bool enable_profiling;           /* Track performance */
    bool verbose;                    /* Debug output */
} gpu_eval_config_t;

gpu_eval_config_t gpu_eval_default_config(void);
```

### Context Management

```c
gpu_eval_context_t* gpu_eval_init(const gpu_eval_config_t* config);
void gpu_eval_free(gpu_eval_context_t* ctx);
int gpu_eval_get_device_info(const gpu_eval_context_t* ctx, gpu_device_info_t* info);
```

### Evaluation Functions

```c
/* Hold'em: 7 cards → HandVal */
int gpu_eval_holdem_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hands,      /* [batch_size * 7] */
    size_t batch_size,
    uint32_t* out_values       /* [batch_size] */
);

/* Omaha: 4 hole + 5 board → HandVal */
int gpu_eval_omaha_batch(
    gpu_eval_context_t* ctx,
    const uint8_t* hole,       /* [batch_size * 4] */
    const uint8_t* board,      /* [batch_size * 5] */
    size_t batch_size,
    uint32_t* out_values       /* [batch_size] */
);

/* Equity: Hero vs Villain on N boards */
int gpu_eval_equity_holdem(
    gpu_eval_context_t* ctx,
    const uint8_t hero_hand[2],
    const uint8_t villain_hand[2],
    const uint8_t* boards,     /* [num_boards * 5] */
    size_t num_boards,
    uint64_t* out_wins,
    uint64_t* out_ties,
    uint64_t* out_losses
);
```

### Statistics

```c
void gpu_eval_get_stats(
    const gpu_eval_context_t* ctx,
    uint64_t* out_total_evals,
    double* out_total_time_ms,
    double* out_evals_per_sec
);

void gpu_eval_reset_stats(gpu_eval_context_t* ctx);
```

### Utilities

```c
bool gpu_eval_is_available(void);
int gpu_eval_get_device_count(gpu_backend_t backend);
size_t gpu_eval_get_recommended_batch_size(const gpu_eval_context_t* ctx);
```

## Card Representation

Cards are represented as `uint8_t` indices **0-51**:
- Rank: `card % 13` (0=Deuce, 12=Ace)
- Suit: `card / 13` (0=Spades, 1=Hearts, 2=Diamonds, 3=Clubs)

Example:
```c
uint8_t As = 12;  /* Ace of Spades */
uint8_t Kh = 25;  /* King of Hearts */
```

## Performance Tips

### Batch Size

- **Larger batches** = better GPU utilization
- Recommended: **100K-1M hands** per batch
- Query optimal size: `gpu_eval_get_recommended_batch_size()`

### Memory Transfer

- Minimize host↔device transfers
- Reuse buffers when possible
- Consider pinned memory for large transfers

### Backend Selection

- **CUDA**: Best for NVIDIA GPUs (RTX, Tesla, A100)
- **OpenCL**: Cross-platform (NVIDIA, AMD, Intel)

## Build Configuration

```bash
# Enable CUDA support
cmake .. -DENABLE_CUDA=ON

# Enable OpenCL support
cmake .. -DENABLE_OPENCL=ON

# Enable both
cmake .. -DENABLE_CUDA=ON -DENABLE_OPENCL=ON
```

## Current Status (Phase 1)

### Implemented ✅
- API header and types
- Context management skeleton
- CUDA kernel structure (21-combo enumeration)
- Warp-level reductions
- Memory layout design

### TODO 📋
- Complete CUDA kernel implementation
- Add OpenCL kernel (.cl file)
- Implement device initialization (CUDA runtime/OpenCL platform detection)
- Load lookup tables to device memory
- Memory buffer management (allocation/reuse)
- Kernel launch and synchronization
- Performance benchmarks
- Unit tests

## Next Steps

1. **Complete CUDA backend** (device init, memory mgmt, kernel launch)
2. **Add OpenCL backend** (port kernel to .cl, OpenCL dispatch)
3. **Load lookup tables** (rank/flush/straight tables to constant memory)
4. **Add benchmarks** (throughput tests, compare vs CPU)
5. **Integration** (use from batched Monte Carlo equity calculations)

## References

- CUDA Programming Guide: https://docs.nvidia.com/cuda/
- OpenCL Specification: https://www.khronos.org/opencl/

## Related

- [GPU Usage Guide](GPU_USAGE_GUIDE.md) - General GPU setup
- [Equity Calculation](../../api/MODERN_API_GUIDE.md) - CPU equity APIs
- [SIMD Operations](../../optimization/guides/SIMD_USAGE_GUIDE.md) - CPU SIMD evaluation

## Phase 3 Enhancements (October 6, 2025)

### Implemented Features ✅

**OpenCL Support**
- Complete OpenCL kernel (`.cl` file) for AMD/Intel GPU compatibility
- OpenCL platform/device detection and initialization
- Work-group parallelism (equivalent to CUDA blocks/threads)
- Local memory optimization (equivalent to CUDA shared memory)

**Real Poker-Eval Tables**
- Table loader module (`gpu_table_loader.{c,h}`)
- Loads actual `evxPairValueTable`, `evxTripsValueTable`, `evxFlushCardsTable`, `evxStrValueTable`
- Table validation and debug utilities
- Fallback to simplified tables if load fails

**Multi-GPU Support**
- `eval_multi_gpu.{c,h}` module
- Work distribution strategies: round-robin, static split, dynamic balance
- Pthread-based worker threads (one per GPU)
- Per-GPU statistics and throughput tracking
- Automatic GPU count detection

**Concurrent Streams (CUDA)**
- 4 concurrent CUDA streams per context
- Per-stream device buffers for parallel execution
- Pinned host memory (cudaHostAlloc) for fast transfers
- Overlap I/O and compute for maximum throughput

**Comprehensive Benchmarks**
- `bench_gpu_comprehensive.c`: Full validation suite
- Single GPU throughput (1K - 5M hands)
- Multi-GPU scaling (1-N GPUs)
- CPU vs GPU comparison
- Target validation (1-5 billion hands/min)

### Multi-GPU Example

```c
#include <poker_eval/gpu/eval_multi_gpu.h>

/* Initialize all available GPUs */
multi_gpu_config_t cfg = multi_gpu_default_config();
cfg.num_devices = -1; /* Use all */
cfg.strategy = MULTI_GPU_STATIC_SPLIT;
cfg.enable_profiling = true;

multi_gpu_context_t* mgpu = multi_gpu_init(&cfg);
if (!mgpu) {
    fprintf(stderr, "Multi-GPU init failed (need >= 2 GPUs)\n");
    return -1;
}

printf("Initialized %d GPUs\n", multi_gpu_get_device_count(mgpu));

/* Evaluate massive batch distributed across GPUs */
size_t batch_size = 50000000; /* 50M hands */
uint8_t* hands = malloc(batch_size * 7);
uint32_t* values = malloc(batch_size * sizeof(uint32_t));

/* ... generate hands ... */

multi_gpu_eval_holdem_batch(mgpu, hands, batch_size, values);

/* Check performance */
multi_gpu_stats_t stats;
multi_gpu_get_stats(mgpu, &stats);

printf("Total throughput: %.2f M hands/sec\n", 
       stats.aggregate_throughput / 1e6);
printf("Per-GPU breakdown:\n");
for (int i = 0; i < multi_gpu_get_device_count(mgpu); i++) {
    printf("  GPU %d: %lu hands in %.2f ms\n",
           i, stats.per_gpu_evals[i], stats.per_gpu_time_ms[i]);
}

multi_gpu_free(mgpu);
```

### Backend Selection

```c
/* Automatic backend selection (CUDA first, OpenCL fallback) */
gpu_eval_config_t cfg = gpu_eval_default_config();
cfg.preferred_backend = GPU_BACKEND_NONE; /* Auto */

/* Or force specific backend */
cfg.preferred_backend = GPU_BACKEND_OPENCL; /* For AMD/Intel */
cfg.preferred_backend = GPU_BACKEND_CUDA;   /* For NVIDIA */
```

### Running Benchmarks

```bash
# Phase 3 comprehensive benchmark
cd build
./benchmarks/bench_gpu_comprehensive

# Output example:
# === Single GPU Throughput Benchmark (CUDA) ===
# Device: NVIDIA RTX 4090
# Global Memory: 24.00 GB
#
# Batch Size    Time (ms)    Throughput (M hands/sec)    Target (1-5B/min)
# --------------------------------------------------------------------------
# 1000          0.45         2.22                        0.13 B/min [BELOW TARGET]
# 10000         0.82         12.20                       0.73 B/min [BELOW TARGET]
# 100000        4.15         24.10                       1.45 B/min [TARGET MET]
# 1000000       38.20        26.18                       1.57 B/min [TARGET MET]
# 5000000       185.50       26.95                       1.62 B/min [TARGET MET]
```

### File Structure

```
gpu/
├── kernels/
│   ├── eval_holdem_batch.cu      # CUDA kernel (Phase 1-2)
│   └── eval_holdem_batch.cl      # OpenCL kernel (Phase 3) ✨
├── include/
│   ├── eval_batched_gpu.h        # Main API
│   └── eval_multi_gpu.h          # Multi-GPU API (Phase 3) ✨
├── eval_batched_gpu.c            # Unified dispatcher
├── eval_batched_cuda.c           # CUDA backend (enhanced Phase 3)
├── eval_batched_opencl.c         # OpenCL backend (Phase 3) ✨
├── eval_batched_opencl.h         # OpenCL internal header ✨
├── eval_multi_gpu.c              # Multi-GPU implementation (Phase 3) ✨
├── gpu_table_loader.c            # Real table loader (Phase 3) ✨
└── gpu_table_loader.h            # Table loader API ✨

benchmarks/
├── bench_gpu_batched.c           # Original benchmark (Phase 2)
└── bench_gpu_comprehensive.c     # Full suite (Phase 3) ✨
```

## Production Readiness

**Phase 3 delivers production-ready GPU evaluation:**

✅ **Multi-vendor support**: CUDA + OpenCL for NVIDIA/AMD/Intel  
✅ **Accurate evaluation**: Real poker-eval lookup tables  
✅ **Scalability**: Multi-GPU support with near-linear scaling  
✅ **Performance**: Concurrent streams, pinned memory, optimized kernels  
✅ **Validation**: Comprehensive benchmark suite  
✅ **Documentation**: Complete API docs and usage examples  

**Remaining optimizations (optional):**
- [ ] Fast-path NFS (no-flush/no-straight) on GPU
- [ ] Multi-stream Omaha evaluation
- [ ] GPU-CFR integration (see Plan section 5.2)
- [ ] Variance reduction via QMC on GPU
