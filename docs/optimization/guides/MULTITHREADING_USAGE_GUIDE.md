# Multithreading Usage Guide for CalculateEquityForRanges

## Overview

This guide explains how to use the different multithreaded versions of `CalculateEquityForRanges` to optimize range equity calculations.

## Available Versions

### 1. Single-threaded version (baseline)
```c
int CalculateEquityForRanges(...)
```
- Usage: Small ranges (<100 matchups)
- Advantages: No overhead, simple
- Disadvantages: Slow for large ranges

### 2. Original MT version
```c
int CalculateEquityForRanges_MT(..., int num_threads)
```
- Usage: Medium ranges (100-10k matchups)
- Advantages: Good general performance
- Disadvantages: High memory usage for large ranges

### 3. MT v2 version (Phase 1 optimized)
```c
int CalculateEquityForRanges_MT_v2(..., int num_threads)
```
- Usage: Medium to large ranges
- Advantages: Better scheduling, prefetching
- Optimizations: Adaptive chunk size, guided scheduling

### 4. MT v3 version (Phase 2 optimized)
```c
int CalculateEquityForRanges_MT_v3(..., int num_threads)
```
- Usage: Very large ranges (>10k matchups)
- Advantages: Low memory usage, scalable
- Optimizations: Lazy matchup generation

### 5. Auto version
```c
int CalculateEquityForRanges_Auto(...)
```
- Usage: Automatic choice of the best version
- Advantages: Optimal for all cases

## Usage Examples

### Example 1: Simple Calculation
```c
#include <poker_eval/equity/RangeEquity.h>

// Define ranges
StdDeck_CardMask hands1[10], hands2[10];
// ... fill hands ...

PlayerRange ranges[2] = {
    {.hand_masks = hands1, .count = 10},
    {.hand_masks = hands2, .count = 10}
};

// Prepare board and dead cards
StdDeck_CardMask board, dead;
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_RESET(dead);

// Calculate equity
enum_result_t result;
enumResultAlloc(&result, 2, enum_ordering_mode_hi);

int matchups = CalculateEquityForRanges_MT_v3(
    game_holdem,    // Game type
    ranges,         // Player ranges
    2,              // Number of players
    board,          // Current board
    dead,           // Dead cards
    5,              // Board cards to deal
    0,              // Exhaustive mode (not Monte Carlo)
    0,              // Iterations (unused in exhaustive)
    0,              // Order flag
    &result,        // Results
    4               // Number of threads
);

// Display results
printf("Player 1: %.2f%%\n", result.ev[0] / matchups * 100);
printf("Player 2: %.2f%%\n", result.ev[1] / matchups * 100);

enumResultFree(&result);
```

### Example 2: Automatic Selection
```c
// Use Auto version for optimal selection
int matchups = CalculateEquityForRanges_Auto(
    game_holdem, ranges, 2, board, dead,
    5, 0, 0, 0, &result
);
```

### Example 3: Large Range with v3
```c
// For very large ranges (e.g., 200+ hands each)
// Use v3 to save memory

// Configure optimal thread count
int num_threads = omp_get_max_threads();
if (num_threads > 8) num_threads = 8; // Limit to 8 threads

int matchups = CalculateEquityForRanges_MT_v3(
    game_holdem, large_ranges, 2, board, dead,
    5, 0, 0, 0, &result, num_threads
);
```

## Performance Recommendations

### Version Selection

| Range Size | Recommended Version | Threads |
|------------|---------------------|---------|
| < 100 matchups | Single-thread | 1 |
| 100-1000 | MT or MT_v2 | 2-4 |
| 1000-10000 | MT_v2 | 4-8 |
| > 10000 | MT_v3 | 4-8 |
| Variable | Auto | Auto |

### Thread Count

```c
// Automatic detection
int threads = 0; // 0 = auto-detect

// Based on physical cores
int threads = omp_get_num_procs() / 2;

// Fixed
int threads = 4; // Good general compromise
```

### Memory Optimization

For very large ranges:
- Use MT_v3 (lazy generation)
- Limit the thread count if memory is constrained
- Consider Monte Carlo mode for >100k matchups

## Compilation

```bash
# With OpenMP
gcc -O3 -fopenmp myapp.c -lpoker_equity -lm
# Or with combined library:
# gcc -O3 -fopenmp myapp.c -lpoker_eval -lm

# Recommended flags
CFLAGS = -O3 -march=native -fopenmp
```

## Debugging and Profiling

### Enable Tracing
```c
#define TRACE_RE(...) fprintf(stderr, __VA_ARGS__)
```

### Measure Performance
```c
#include <sys/time.h>

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

double start = get_time();
// ... calculation ...
double elapsed = get_time() - start;
printf("Time: %.3f seconds\n", elapsed);
```

### Environment Variables
```bash
# Control OpenMP
export OMP_NUM_THREADS=4
export OMP_PROC_BIND=true
export OMP_PLACES=cores

# Profiling
export OMP_DISPLAY_ENV=true
```

## Troubleshooting

### Disappointing Performance
1. Check range size (are they too small?)
2. Test different thread counts
3. Use the appropriate version
4. Compile with -O3

### Incorrect Results
1. Check structure initialization
2. Ensure enumResultAlloc is called
3. Do not forget enumResultFree
4. Divide ev[] by matchups for %

### High Memory Usage
1. Switch to MT_v3 for large ranges
2. Reduce thread count
3. Use Monte Carlo if appropriate

## Complete Examples

See under `examples/`:
- `test_mt_optimization.c`: Simple test
- `benchmark_all_versions.c`: Full comparison
- `benchmark_large_ranges.c`: Large range test
- `range_equity_mt_example.c`: Various examples