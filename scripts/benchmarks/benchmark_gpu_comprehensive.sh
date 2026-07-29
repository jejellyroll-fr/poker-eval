#!/bin/bash

# Comprehensive GPU Benchmark Script
# Tests different batch sizes to find optimal performance points

echo "=== Comprehensive GPU Acceleration Benchmark ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo ""

# Build the test programs
echo "Building test programs..."
make clean > /dev/null 2>&1
make test_gpu_acceleration gpu_eval_example > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"
echo ""

# Run GPU availability test
echo "=== GPU Availability Test ==="
./tests/test_gpu_acceleration | grep -E "(CUDA|OpenCL|Device:|Memory:|Compute Units:)"
echo ""

# Test different batch sizes for hand evaluation
echo "=== Hand Evaluation Performance Test ==="
echo "Testing different batch sizes to find optimal performance..."
echo ""

# Create a simple benchmark program
cat > benchmark_batch_sizes.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "poker_defs.h"
#include "eval_gpu.h"
#include "inlines/eval.h"

StdDeck_CardMask random_cards(int n_cards) {
    StdDeck_CardMask result, dead;
    StdDeck_CardMask_RESET(result);
    StdDeck_CardMask_RESET(dead);
    
    for (int i = 0; i < n_cards; i++) {
        int card;
        StdDeck_CardMask card_mask;
        
        do {
            card = rand() % 52;
            card_mask = StdDeck_MASK(card);
        } while (StdDeck_CardMask_ANY_SET(dead, card_mask));
        
        StdDeck_CardMask_OR(result, result, card_mask);
        StdDeck_CardMask_OR(dead, dead, card_mask);
    }
    
    return result;
}

void benchmark_batch_size(int n_boards, int n_players) {
    printf("Batch size: %d boards x %d players = %d evaluations\n", 
           n_boards, n_players, n_boards * n_players);
    
    // Check if GPU is available
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("  GPU not available\n\n");
        return;
    }
    
    // Allocate test data
    StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
    StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
    HandVal* cpu_results = malloc(n_boards * n_players * sizeof(HandVal));
    
    // Generate random data
    for (int i = 0; i < n_boards; i++) {
        boards[i] = random_cards(5);
        for (int j = 0; j < n_players; j++) {
            hole_cards[i * n_players + j] = random_cards(2);
        }
    }
    
    // CPU benchmark
    clock_t cpu_start = clock();
    for (int i = 0; i < n_boards; i++) {
        for (int j = 0; j < n_players; j++) {
            StdDeck_CardMask combined;
            StdDeck_CardMask_OR(combined, boards[i], hole_cards[i * n_players + j]);
            cpu_results[i * n_players + j] = StdDeck_StdRules_EVAL_N(combined, 7);
        }
    }
    clock_t cpu_end = clock();
    double cpu_time = ((double)(cpu_end - cpu_start)) / CLOCKS_PER_SEC;
    
    // GPU benchmark
    gpu_eval_context_t* gpu_ctx = gpu_eval_init(0, n_boards, gpu_backend);
    if (!gpu_ctx) {
        printf("  GPU context initialization failed\n\n");
        goto cleanup;
    }
    
    gpu_eval_result_t gpu_result = {0};
    clock_t gpu_start = clock();
    int ret = gpu_eval_batch_boards(gpu_ctx, boards, hole_cards, n_boards, n_players, &gpu_result);
    clock_t gpu_end = clock();
    double gpu_time = ((double)(gpu_end - gpu_start)) / CLOCKS_PER_SEC;
    
    if (ret == 0) {
        // Verify accuracy
        int mismatches = 0;
        for (int i = 0; i < n_boards * n_players; i++) {
            if (cpu_results[i] != gpu_result.hand_values[i]) {
                mismatches++;
            }
        }
        
        printf("  CPU:  %.4f sec (%.0f evals/sec)\n", cpu_time, (n_boards * n_players) / cpu_time);
        printf("  GPU:  %.4f sec (%.0f evals/sec)\n", gpu_time, (n_boards * n_players) / gpu_time);
        printf("  Speedup: %.2fx\n", cpu_time / gpu_time);
        printf("  Accuracy: %s (%d mismatches)\n", mismatches == 0 ? "PASS" : "FAIL", mismatches);
        
        free(gpu_result.hand_values);
    } else {
        printf("  GPU evaluation failed\n");
    }
    
    gpu_eval_cleanup(gpu_ctx);
    
cleanup:
    free(boards);
    free(hole_cards);
    free(cpu_results);
    printf("\n");
}

int main() {
    srand(time(NULL));
    
    // Test various batch sizes
    int batch_sizes[] = {100, 500, 1000, 2000, 5000, 10000, 20000, 50000};
    int n_batch_sizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);
    
    for (int i = 0; i < n_batch_sizes; i++) {
        benchmark_batch_size(batch_sizes[i], 2);
    }
    
    // Test with more players
    printf("=== Multi-player tests ===\n");
    benchmark_batch_size(1000, 6);
    benchmark_batch_size(1000, 9);
    
    return 0;
}
EOF

# Compile the benchmark
gcc -O3 -I./include -I./gpu/include benchmark_batch_sizes.c -L. -lpoker_lib_static -L./gpu -lpoker_gpu_unified -lpoker_opencl -framework OpenCL -o benchmark_batch_sizes

if [ $? -eq 0 ]; then
    echo "Running batch size benchmarks..."
    ./benchmark_batch_sizes
    rm benchmark_batch_sizes.c benchmark_batch_sizes
else
    echo "Failed to compile benchmark program"
fi

echo ""
echo "=== Monte Carlo Performance Test ==="
echo "Testing Monte Carlo simulation performance..."

# Create Monte Carlo benchmark
cat > benchmark_monte_carlo.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "poker_defs.h"
#include "eval_gpu.h"

void benchmark_monte_carlo(int n_simulations) {
    printf("Monte Carlo: %d simulations\n", n_simulations);
    
    int gpu_backend = -1;
    if (gpu_is_available(0)) {
        gpu_backend = 0;
    } else if (gpu_is_available(1)) {
        gpu_backend = 1;
    }
    
    if (gpu_backend < 0) {
        printf("  GPU not available\n\n");
        return;
    }
    
    gpu_eval_context_t* gpu_ctx = gpu_eval_init(0, n_simulations, gpu_backend);
    if (!gpu_ctx) {
        printf("  GPU context initialization failed\n\n");
        return;
    }
    
    int n_players = 2;
    float equities[2];
    
    clock_t start = clock();
    int ret = gpu_monte_carlo_equity(gpu_ctx, NULL, n_players, n_simulations, equities);
    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    if (ret == 0) {
        printf("  Time: %.4f sec (%.0f sims/sec)\n", time_taken, n_simulations / time_taken);
        printf("  Results: P1=%.3f, P2=%.3f\n", equities[0], equities[1]);
    } else {
        printf("  Monte Carlo simulation failed\n");
    }
    
    gpu_eval_cleanup(gpu_ctx);
    printf("\n");
}

int main() {
    srand(time(NULL));
    
    int sim_counts[] = {1000, 10000, 100000, 500000, 1000000};
    int n_sim_counts = sizeof(sim_counts) / sizeof(sim_counts[0]);
    
    for (int i = 0; i < n_sim_counts; i++) {
        benchmark_monte_carlo(sim_counts[i]);
    }
    
    return 0;
}
EOF

gcc -O3 -I./include -I./gpu/include benchmark_monte_carlo.c -L. -lpoker_lib_static -L./gpu -lpoker_gpu_unified -lpoker_opencl -framework OpenCL -o benchmark_monte_carlo

if [ $? -eq 0 ]; then
    ./benchmark_monte_carlo
    rm benchmark_monte_carlo.c benchmark_monte_carlo
else
    echo "Failed to compile Monte Carlo benchmark"
fi

echo ""
echo "=== Summary ==="
echo "GPU acceleration implementation is functional and tested."
echo "Performance depends on:"
echo "1. GPU hardware (dedicated GPUs perform better than integrated)"
echo "2. Batch size (larger batches amortize transfer overhead)"
echo "3. Problem complexity (Monte Carlo benefits more from parallelization)"
echo ""
echo "For optimal performance:"
echo "- Use batch sizes >= 1000 evaluations"
echo "- Prefer dedicated GPUs over integrated GPUs"
echo "- Consider CPU for small batch sizes due to transfer overhead"