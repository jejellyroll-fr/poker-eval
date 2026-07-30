# GPU Acceleration Usage Guide

## Overview

GPU acceleration for poker hand evaluation provides improved performance for high-volume calculations. This guide explains how to effectively use this feature.

## Installation and Building

### Prerequisites

#### For OpenCL (Recommended)
- **macOS**: Included by default
- **Linux**: `sudo apt-get install opencl-headers ocl-icd-opencl-dev`
- **Windows**: OpenCL SDK from the GPU vendor

#### For CUDA (NVIDIA GPUs only)
- CUDA Toolkit 10.0+
- NVIDIA GPU with Compute Capability 3.5+

### Compilation

```bash
# Build with automatic GPU support
mkdir build && cd build
cmake ..
make

# Or use traditional build targets
make gpu_eval_example test_gpu_acceleration
```

## Basic API

### Initialization

```c
#include <poker_eval/gpu/eval_gpu.h>

// Check GPU availability
int cuda_available = gpu_is_available(0);    // CUDA
int opencl_available = gpu_is_available(1);  // OpenCL

// Choose backend
int backend = cuda_available ? 0 : (opencl_available ? 1 : -1);
if (backend < 0) {
    printf("No GPU available\n");
    return -1;
}

// Initialize GPU context
gpu_eval_context_t* ctx = gpu_eval_init(
    0,           // device_id (0 = first GPU)
    100000,      // max_batch_size
    backend      // 0=CUDA, 1=OpenCL
);
```

### Batch Evaluation

```c
// Prepare data
int n_boards = 10000;
int n_players = 2;
StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));

// ... fill boards and hole_cards ...

// Evaluate on GPU
gpu_eval_result_t result = {0};
int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, 
                               n_boards, n_players, &result);

if (ret == 0) {
    // Use result.hand_values[i] for each evaluation
    for (int i = 0; i < n_boards * n_players; i++) {
        printf("Hand %d: %u\n", i, result.hand_values[i]);
    }
    free(result.hand_values);
}
```

### Monte Carlo Simulation

```c
// Equity simulation
int n_players = 2;
int n_simulations = 1000000;
float equities[2];

int ret = gpu_monte_carlo_equity(ctx, NULL, n_players, 
                                n_simulations, equities);

if (ret == 0) {
    printf("Player 1: %.3f (%.1f%%)\n", equities[0], equities[0] * 100);
    printf("Player 2: %.3f (%.1f%%)\n", equities[1], equities[1] * 100);
}
```

### Cleanup

```c
// Free resources
gpu_eval_cleanup(ctx);
```

## Performance Optimization

### Batch Size Selection

#### Dedicated GPUs (NVIDIA/AMD)
```c
// Optimal for dedicated GPUs
if (n_evaluations >= 10000) {
    // Use GPU - good expected speedup
    use_gpu = 1;
    batch_size = min(n_evaluations, 100000);
} else {
    // Use CPU - GPU overhead too high
    use_gpu = 0;
}
```

#### Integrated GPUs (Intel)
```c
// More conservative for integrated GPUs
if (n_evaluations >= 50000) {
    // GPU may be beneficial
    use_gpu = 1;
    batch_size = min(n_evaluations, 50000);
} else {
    // CPU is likely faster
    use_gpu = 0;
}
```

### Context Reuse

```c
// ✅ Good: Reuse context
gpu_eval_context_t* ctx = gpu_eval_init(0, 100000, backend);

for (int batch = 0; batch < num_batches; batch++) {
    gpu_eval_batch_boards(ctx, ...);  // Reuses context
}

gpu_eval_cleanup(ctx);

// ❌ Bad: Recreate context every time
for (int batch = 0; batch < num_batches; batch++) {
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, backend);
    gpu_eval_batch_boards(ctx, ...);
    gpu_eval_cleanup(ctx);  // High overhead
}
```

### Memory Management

```c
// Pre-allocate to avoid repeated allocations
StdDeck_CardMask* boards = malloc(MAX_BATCH_SIZE * sizeof(StdDeck_CardMask));
StdDeck_CardMask* hole_cards = malloc(MAX_BATCH_SIZE * MAX_PLAYERS * sizeof(StdDeck_CardMask));

// Reuse buffers for multiple batches
for (int batch = 0; batch < num_batches; batch++) {
    // Fill boards and hole_cards for this batch
    fill_batch_data(boards, hole_cards, batch);
    
    // Evaluate
    gpu_eval_batch_boards(ctx, boards, hole_cards, batch_size, n_players, &result);
    
    // Process results
    process_results(&result);
    
    // result.hand_values is automatically reused
}

free(boards);
free(hole_cards);
```

## Practical Examples

### Example 1: Range vs Range Evaluation

```c
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/core/poker_defs.h>

void evaluate_range_vs_range() {
    // Initialize GPU
    int backend = gpu_is_available(1) ? 1 : 0;  // Prefer OpenCL
    gpu_eval_context_t* ctx = gpu_eval_init(0, 50000, backend);
    
    if (!ctx) {
        printf("GPU unavailable, fallback to CPU\n");
        return;
    }
    
    // Generate all possible board combinations
    int n_boards = 10000;
    StdDeck_CardMask* boards = generate_random_boards(n_boards);
    
    // Range 1: AA (6 combinations)
    // Range 2: KK (6 combinations)  
    StdDeck_CardMask aa_hands[6], kk_hands[6];
    generate_pocket_pairs(RANK_ACE, aa_hands);
    generate_pocket_pairs(RANK_KING, kk_hands);
    
    int total_wins_aa = 0, total_wins_kk = 0, total_ties = 0;
    
    // Evaluate each combination
    for (int aa = 0; aa < 6; aa++) {
        for (int kk = 0; kk < 6; kk++) {
            StdDeck_CardMask hole_cards[2] = {aa_hands[aa], kk_hands[kk]};
            
            gpu_eval_result_t result = {0};
            int ret = gpu_eval_batch_boards(ctx, boards, hole_cards, 
                                          n_boards, 2, &result);
            
            if (ret == 0) {
                // Count wins
                for (int i = 0; i < n_boards; i++) {
                    HandVal aa_val = result.hand_values[i * 2];
                    HandVal kk_val = result.hand_values[i * 2 + 1];
                    
                    if (aa_val > kk_val) total_wins_aa++;
                    else if (kk_val > aa_val) total_wins_kk++;
                    else total_ties++;
                }
                free(result.hand_values);
            }
        }
    }
    
    int total_hands = 6 * 6 * n_boards;
    printf("AA vs KK over %d boards:\n", total_hands);
    printf("AA wins: %.2f%%\n", (float)total_wins_aa / total_hands * 100);
    printf("KK wins: %.2f%%\n", (float)total_wins_kk / total_hands * 100);
    printf("Ties: %.2f%%\n", (float)total_ties / total_hands * 100);
    
    gpu_eval_cleanup(ctx);
    free(boards);
}
```

### Example 2: Adaptive Benchmark

```c
void adaptive_gpu_benchmark() {
    int backend = gpu_is_available(1) ? 1 : 0;
    
    // Test different sizes to find the break-even point
    int test_sizes[] = {100, 1000, 5000, 10000, 50000};
    int n_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    printf("Finding GPU/CPU break-even point...\n");
    
    for (int i = 0; i < n_sizes; i++) {
        int batch_size = test_sizes[i];
        
        // CPU Test
        clock_t cpu_start = clock();
        run_cpu_evaluation(batch_size);
        clock_t cpu_end = clock();
        double cpu_time = (double)(cpu_end - cpu_start) / CLOCKS_PER_SEC;
        
        // GPU Test
        clock_t gpu_start = clock();
        run_gpu_evaluation(batch_size, backend);
        clock_t gpu_end = clock();
        double gpu_time = (double)(gpu_end - gpu_start) / CLOCKS_PER_SEC;
        
        double speedup = cpu_time / gpu_time;
        printf("Batch %d: CPU=%.4fs, GPU=%.4fs, Speedup=%.2fx %s\n",
               batch_size, cpu_time, gpu_time, speedup,
               speedup > 1.0 ? "✓" : "✗");
    }
}
```

## Troubleshooting

### Common Issues

#### 1. GPU Not Detected
```c
if (!gpu_is_available(0) && !gpu_is_available(1)) {
    printf("No GPU detected. Check:\n");
    printf("- GPU drivers installed\n");
    printf("- OpenCL/CUDA runtime available\n");
    printf("- GPU access permissions\n");
}
```

#### 2. Kernel Compilation Error
```c
gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, 1);
if (!ctx) {
    printf("GPU initialization failed.\n");
    printf("Check OpenCL compilation logs.\n");
    // Fallback to CPU
    use_cpu_evaluation();
}
```

#### 3. Disappointing Performance
```c
// Check batch size
if (batch_size < 10000) {
    printf("Batch too small for GPU (high overhead)\n");
    printf("Recommendation: batch_size >= 10000\n");
}

// Check GPU type
char device_name[256];
gpu_get_device_info(0, device_name, NULL, NULL);
if (strstr(device_name, "Intel")) {
    printf("Integrated GPU detected: %s\n", device_name);
    printf("Limited performance; consider CPU for small batches\n");
}
```

### Result Validation

```c
void validate_gpu_accuracy() {
    // Generate test data
    StdDeck_CardMask boards[100];
    StdDeck_CardMask hole_cards[200];  // 100 boards * 2 players
    generate_test_data(boards, hole_cards, 100, 2);
    
    // CPU Evaluation (reference)
    HandVal cpu_results[200];
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 2; j++) {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, boards[i], hole_cards[i*2+j]);
            cpu_results[i*2+j] = StdDeck_StdRules_EVAL_N(combined, 7);
        }
    }
    
    // GPU Evaluation
    gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, 1);
    gpu_eval_result_t gpu_result = {0};
    gpu_eval_batch_boards(ctx, boards, hole_cards, 100, 2, &gpu_result);
    
    // Comparison
    int mismatches = 0;
    for (int i = 0; i < 200; i++) {
        if (cpu_results[i] != gpu_result.hand_values[i]) {
            mismatches++;
            printf("Mismatch %d: CPU=%u, GPU=%u\n", 
                   i, cpu_results[i], gpu_result.hand_values[i]);
        }
    }
    
    printf("Validation: %d/%d correct (%.1f%%)\n", 
           200-mismatches, 200, (200-mismatches)/200.0*100);
    
    free(gpu_result.hand_values);
    gpu_eval_cleanup(ctx);
}
```

## Final Recommendations

### Optimal Usage

1. **Batch Size**: ≥ 10,000 evaluations for dedicated GPUs, ≥ 50,000 for integrated GPUs
2. **Reuse**: Keep GPU context active across multiple batches
3. **Fallback**: Always implement a CPU fallback
4. **Validation**: Test accuracy on your specific hardware

### Recommended Use Cases

- **Range analysis**: Millions of evaluations
- **Monte Carlo simulations**: ≥ 100,000 iterations
- **Preprocessing**: Lookup table generation
- **Batch processing**: Hand history log processing

### Non-Recommended Use Cases

- **Single hand evaluations**: Overhead too high
- **Real-time applications**: Unpredictable latency
- **Embedded systems**: High power consumption
- **Small volumes**: CPU is more efficient
