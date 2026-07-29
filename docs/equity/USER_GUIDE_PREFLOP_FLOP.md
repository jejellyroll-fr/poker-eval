# User Guide: Pre-flop and Flop Equity

This guide explains how to use the `poker-eval` library for pre-flop range analysis and flop equity calculations.

## 1. Pre-flop Equity

The pre-flop module allows you to work with hand ranges and calculate equity before any community cards are dealt.

### Basic Usage

To calculate the equity between two specific hands (e.g., "AKs" vs "qq"):

```c
#include <poker_eval/equity/preflop_equity.h>
#include <stdio.h>

int main() {
    preflop_range_t r1, r2;
    preflop_range_parse("AKs", &r1);
    preflop_range_parse("QQ", &r2);

    preflop_equity_input_t input;
    input.range1 = r1;
    input.range2 = r2;
    input.num_samples = 0; // Exhaustive
    input.lookup_table = NULL;

    preflop_equity_result_t result;
    preflop_equity_calculate(&input, &result);

    printf("AKs vs QQ: %.2f%% vs %.2f%%\n",
           result.equity1 * 100.0, result.equity2 * 100.0);
    return 0;
}
```

### Working with Ranges

The library supports standard range notation, including the `+` suffix for pairs.

```c
preflop_range_t range;
preflop_range_init(&range);

// Add specific hands
preflop_range_parse("AKs, AQs, KQs", &range);

// Add a pair range (QQ, KK, AA)
preflop_range_parse("QQ+", &range);

// Check if a hand is in the range
preflop_hand_t ak_off = preflop_string_to_hand("AKo");
if (preflop_range_contains(&range, ak_off)) {
    // ...
}
```

### Performance Optimization

For high-volume calculations, you can use Monte Carlo sampling instead of exhaustive enumeration:

```c
input.num_samples = 10000; // Run 10,000 simulations
```

Alternatively, you can load a lookup table for instant 1v1 equity results:

```c
preflop_lookup_table_t *table = preflop_lookup_table_load("preflop.bin", game_holdem);
input.lookup_table = table;
// Calculation is now O(1)
```

## 2. Flop Analysis

The flop module provides tools to analyze the texture of the flop and calculate post-flop equity.

### Texture Analysis

Understanding the board texture is crucial for strategy. The library categorizes flops into:
*   **Dry**: Disconnected, uncoordinated (e.g., K 7 2 rainbow).
*   **Wet**: Highly coordinated (e.g., 9 8 7 two-tone).
*   **Coordinated**: Some connectivity.
*   **Paired**: Contains a pair.
*   **Trips**: Contains three of a kind.

```c
#include <poker_eval/equity/flop_equity.h>

StdDeck_CardMask flop;
// ... initialize flop ...

flop_analysis_t analysis;
analyze_flop_texture(flop, &analysis);

if (analysis.is_monotone) {
    printf("Flop is monotone! Flush danger.\n");
}

printf("Texture Score: %d/100\n", analysis.texture_score);
```

### Flop Equity Calculation

Calculate your equity and draw probabilities on the flop.

```c
flop_equity_input_t input;
input.pocket = my_pocket;
input.flop = my_flop;
input.n_opponents = 1;
input.n_samples = 0; // Exhaustive

flop_equity_result_t result;
flop_calc_equity(&input, &result);

printf("Equity: %.2f%%\n", result.equity * 100.0);
printf("Flush Draw Probability: %.2f%%\n", result.prob_flush_draw * 100.0);
printf("Turn Improvement: %.2f%%\n", result.turn_improvement * 100.0);
```

## 3. Benchmarking

The library includes benchmarks to measure performance. You can build them by enabling the `BUILD_BENCHMARKS` option in CMake (or by using the provided build scripts which may enable them by default in development mode).

Executables:
*   `bin/bench_preflop_equity`: Benchmarks range vs range calculation.
*   `bin/bench_flop_equity`: Benchmarks flop equity calculation.

Note: Exhaustive enumeration can be slow. Adjust `num_samples` for trade-off between speed and accuracy.
