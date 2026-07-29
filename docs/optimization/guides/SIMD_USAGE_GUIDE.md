# SIMD Vectorization Usage Guide

## Overview

This guide explains how to use the SIMD (Single Instruction, Multiple Data) vectorization features in poker-eval to accelerate hand evaluation. The SIMD implementation can process multiple poker hands simultaneously, providing significant performance improvements on modern CPUs.

## Features

- **Automatic CPU detection**: Detects available SIMD capabilities (SSE2, AVX2, AVX-512)
- **Batch evaluation**: Process 4 hands (AVX2) or 8 hands (AVX-512) simultaneously
- **Adaptive API**: Automatically selects the best available SIMD implementation
- **Backward compatible**: Falls back to scalar evaluation on unsupported CPUs

## Performance Benefits

Typical speedups observed:
- AVX2: 2-3x faster for batch evaluation
- AVX-512: 3-4x faster for batch evaluation
- Best results when evaluating large numbers of hands

## API Reference

### Capability Detection

```c
#include "simd_card_operations.h"

// Detect available SIMD capabilities
simd_capability_t cap = simd_detect_capability();
printf("SIMD capability: %s\n", simd_capability_name(cap));
```

### Simple Batch Evaluation

```c
// Evaluate multiple hands at once
StdDeck_CardMask hands[100];
HandVal results[100];

// ... populate hands array ...

// Evaluate all hands using SIMD
simd_eval_multiple_hands(hands, 100, results);
```

### Advanced Batch Processing

```c
// Prepare batch manually for more control
simd_card_batch_t batch;
simd_result_batch_t results;

// Convert card masks to SIMD format
simd_prepare_batch_from_masks(hands, 8, &batch);

// Evaluate using specific SIMD version
simd_eval_batch_hands_avx2(&batch, &results);

// Extract results
HandVal hand_values[8];
simd_extract_results_to_array(&results, hand_values);
```

### Range vs Range Equity

```c
// Calculate equity between two ranges
StdDeck_CardMask range1[10], range2[10];
StdDeck_CardMask board;

// ... populate ranges and board ...

double equity = simd_calculate_range_equity(
    range1, 10,      // Range 1
    range2, 10,      // Range 2
    &board, 5,       // Board cards
    10000            // Iterations
);
```

## Example: Tournament Simulation

```c
// Simulate multiple tables simultaneously
void simulate_tournament_round() {
    const int num_tables = 100;
    const int players_per_table = 9;
    
    StdDeck_CardMask all_hands[900];  // All player hands
    HandVal all_results[900];
    
    // Deal cards to all players
    deal_tournament_hands(all_hands, num_tables, players_per_table);
    
    // Evaluate all hands in one call
    simd_eval_multiple_hands(all_hands, 900, all_results);
    
    // Determine winners for each table
    for (int table = 0; table < num_tables; table++) {
        int winner = find_table_winner(
            &all_results[table * players_per_table],
            players_per_table
        );
        printf("Table %d winner: Player %d\n", table + 1, winner + 1);
    }
}
```

## Building with SIMD Support

### CMake

The SIMD features are automatically included when building with CMake:

```bash
mkdir build
cd build
cmake ..
make
```

### Manual Compilation

To enable SIMD optimizations when compiling manually:

```bash
# For AVX2 support
gcc -mavx2 -O3 -c lib/simd_card_operations.c

# For AVX-512 support
gcc -mavx512f -O3 -c lib/simd_card_operations.c
```

## Testing and Validation

Run the SIMD test suite to verify functionality:

```bash
./test_simd_operations
```

Benchmark SIMD performance:

```bash
./simd_range_equity_example
```

## Best Practices

1. **Batch Size**: Process hands in multiples of 4 (AVX2) or 8 (AVX-512) for best performance
2. **Memory Alignment**: Ensure arrays are properly aligned for SIMD operations
3. **Validation**: Always validate SIMD results against scalar implementation during development
4. **Fallback**: Implement scalar fallback for CPUs without SIMD support

## Troubleshooting

### No Performance Improvement

- Check CPU capabilities: `cat /proc/cpuinfo | grep -E 'avx2|avx512'`
- Ensure compiler optimizations are enabled: `-O3`
- Verify batch sizes are appropriate for the SIMD version

### Incorrect Results

- Run validation tests: `./test_simd_operations`
- Check for memory alignment issues
- Ensure input data is properly formatted

### Compilation Errors

- Verify compiler supports target SIMD instructions
- Use appropriate compiler flags for the target architecture
- Consider using `-march=native` for automatic CPU detection

## Future Enhancements

- ARM NEON support for mobile/embedded devices
- GPU acceleration integration
- Specialized SIMD kernels for specific game types
- Cache-aware batch processing

## Performance Monitoring

Monitor SIMD performance in production:

```c
// Benchmark different implementations
double scalar_time = benchmark_scalar_evaluation(hands, count);
double simd_time = simd_benchmark_capability(SIMD_AVX2, iterations);

printf("Performance improvement: %.2fx\n", scalar_time / simd_time);
```

## Integration with Existing Code

The SIMD API is designed to integrate seamlessly with existing poker-eval code:

```c
// Before (scalar)
for (int i = 0; i < num_hands; i++) {
    results[i] = StdDeck_StdRules_EVAL_N(hands[i], 7);
}

// After (SIMD)
simd_eval_multiple_hands(hands, num_hands, results);
```

No other code changes required!