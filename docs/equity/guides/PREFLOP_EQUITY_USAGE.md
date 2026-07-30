# Pre-flop Equity System - User Guide

## Overview

The pre-flop equity calculation system allows calculating equity between hands or hand ranges in pre-flop Texas Hold'em.

### Key Concepts

1. **Canonical hand**: Unique representation of a hand (169 possible)
   - 13 pairs (AA, KK, ..., 22)
   - 78 suited hands (AKs, AQs, ..., 32s)
   - 78 offsuit hands (AKo, AQo, ..., 32o)

2. **Specific combinations**: Concrete cards for a canonical hand
   - Pairs: 6 combinations (e.g., AA = AsAh, AsAd, AsAc, AhAd, AhAc, AdAc)
   - Suited: 4 combinations (e.g., AKs = AsKs, AhKh, AdKd, AcKc)
   - Offsuit: 12 combinations (e.g., AKo = AsKh, AsKd, etc.)

3. **Range**: Set of canonical hands
   - Example: "AA,KK,QQ" = 3 canonical hands = 18 total combinations

## Usage

### 1. Exhaustive calculation (100% accurate, slow)

```bash
./bin/preflop_equity_demo "AA" "KK"
```

**Output:**
```
Range 1: AA (1 canonical hands = 6 combos)
Range 2: KK (1 canonical hands = 6 combos)

=== Results ===
Range 1 equity: 81.95%
Range 2 equity: 18.05%
Time: ~1-2 seconds
Boards evaluated: 61,642,944
```

**Calculation breakdown:**
- 6 combos AA × 6 combos KK = 36 specific matchups
- Each matchup enumerates C(48,5) = 1,712,304 boards
- Total: 36 × 1,712,304 = 61,642,944 evaluations
- Time: ~1-2 seconds for head-to-head hand evaluation

### 2. Lookup table calculation (instant)

**Table generation (one-time process):**
```bash
./bin/generate_preflop_table holdem_preflop_169x169.dat
```

⚠️ **Warning**: This operation takes **several hours** (~3-5h)
- 14,365 unique calculations (utilizes symmetry)
- Each calculation = 1-10 seconds depending on hands
- Resulting file: ~114 KB

**Using the table:**
```bash
./bin/preflop_with_table_demo "AA" "KK" holdem_preflop_169x169.dat
```

**Expected output:**
```
=== Method 1: Exhaustive Calculation ===
Equity 1: 81.95%
Time: 1.186 seconds

=== Method 2: Lookup Table ===
Equity 1: 81.95%
Time: 0.000010 seconds

Speedup: 100,000× faster
```

### 3. Range vs Range

```bash
./bin/preflop_equity_demo "AA,KK,QQ" "JJ,TT,99"
```

**Output:**
```
Range 1: 3 canonical hands, 18 combinations
Range 2: 3 canonical hands, 18 combinations
Total matchups: 3 × 3 = 9 canonical pairs

Range 1 equity: 82.31%
Range 2 equity: 17.69%
```

**With lookup table (instant):**
```bash
# When the table has been generated
./bin/preflop_with_table_demo "AA,KK,QQ" "JJ,TT,99" holdem_preflop_169x169.dat
```

## C API

### Basic Structure

```c
#include <poker_eval/equity/preflop_equity.h>

/* Parse a range from string */
preflop_range_t range1;
preflop_range_parse("AA,KK,QQ", &range1);

/* Count combinations */
int combos = preflop_range_count_combinations(&range1);
// combos = 18 (6+6+6)

/* Exhaustive equity calculation */
preflop_equity_input_t input = {
    .range1 = range1,
    .range2 = range2,
    .num_samples = 0,        /* 0 = exhaustive */
    .lookup_table = NULL     /* NULL = no table */
};

preflop_equity_result_t result;
preflop_equity_calculate(&input, &result);

printf("Equity: %.2f%%\n", result.equity1 * 100);
preflop_equity_result_free(&result);
```

### With Lookup Table

```c
/* Load table */
preflop_lookup_table_t *table =
    preflop_lookup_table_load("holdem_preflop_169x169.dat", game_holdem);

if (table) {
    /* Instant calculation */
    input.lookup_table = table;
    preflop_equity_calculate(&input, &result);

    preflop_lookup_table_free(table);
}
```

## Performance

### Calculation Time (exhaustive)

| Matchup | Combinations | Boards Evaluated | Time |
|---------|-------------|------------------|------|
| AA vs KK | 6 × 6 = 36 | 61,642,944 | ~1.2s |
| AKs vs QQ | 4 × 6 = 24 | 41,095,296 | ~0.8s |
| 3×3 Range | ~324 matchups | ~555M boards | ~10s |
| 10×10 Range | ~3600 matchups | ~6B boards | ~2min |

### With Lookup Table

| Matchup | Time |
|---------|------|
| 1 vs 1 | ~10 nanoseconds |
| 3×3 Range | ~100 nanoseconds |
| 10×10 Range | ~1 microsecond |
| 169×169 Range | ~30 microseconds |

**Speedup: ~100,000,000× faster**

## Table Format

### Binary File

```
Offset  | Size   | Description
--------|--------|------------------
0x0000  | 4      | Magic: 0x50464C54 ("PFLT")
0x0004  | 4      | Version: 1
0x0008  | 4      | Game type: game_holdem
0x000C  | 114,244| Equity matrix (169×169 floats)
```

### Properties

- **Size**: 114 KB
- **Symmetry**: equity(A,B) + equity(B,A) = 1.0
- **Precision**: 32-bit float (~6 decimal places)
- **Portability**: Native binary (endianness dependent)

## Validation

### Known Values (vs PokerStove)

| Matchup | Expected Equity | Our Result |
|---------|-----------------|------------|
| AA vs KK | 81.95% vs 18.05% | ✅ 81.95% vs 18.05% |
| AKs vs QQ | 45.93% vs 54.07% | ⏳ To be tested |
| 72o vs AA | ~12% vs ~88% | ⏳ To be tested |

## Current Limitations

1. **Slow table generation**: Takes several hours instead of minutes
   - Potential solution: Parallelization with OpenMP
   - Potential solution: SIMD acceleration for evaluations

2. **Simple range parser**: Supports only comma-separated lists
   - Not supported: complex ranges ("JJ+", "ATo+", etc.)
   - To be implemented in a future version

3. **Hold'em only**: Not yet adapted for pre-flop Omaha

## Next Steps

1. ✅ Implement exhaustive calculation
2. ✅ Create lookup table system
3. ✅ Test accuracy (AA vs KK)
4. ⏳ Optimize table generation
5. ⏳ Validate vs PokerStove/Equilab
6. ⏳ Implement flop equity
7. ⏳ Advanced range parser

## Complete Example

```c
#include <poker_eval/equity/preflop_equity.h>
#include <stdio.h>

int main() {
    /* Parse ranges */
    preflop_range_t range1, range2;
    preflop_range_parse("AA,KK,QQ", &range1);
    preflop_range_parse("JJ,TT,99", &range2);

    printf("Range 1: %d canonical hands = %d combos\n",
           range1.num_hands,
           preflop_range_count_combinations(&range1));

    /* Load lookup table (optional) */
    preflop_lookup_table_t *table =
        preflop_lookup_table_load("holdem_preflop_169x169.dat", game_holdem);

    /* Calculate equity */
    preflop_equity_input_t input = {
        .range1 = range1,
        .range2 = range2,
        .num_samples = 0,
        .lookup_table = table  /* NULL for exhaustive */
    };

    preflop_equity_result_t result;
    preflop_equity_calculate(&input, &result);

    printf("Range 1: %.2f%%\n", result.equity1 * 100);
    printf("Range 2: %.2f%%\n", result.equity2 * 100);

    /* Cleanup */
    preflop_equity_result_free(&result);
    if (table) preflop_lookup_table_free(table);

    return 0;
}
```

## Support

For questions or bugs:
- Tests: `tests/test_preflop_equity.c`
- Documentation: `docs/equity/API_REFERENCE.md`
- Examples: `src/examples/preflop_*`

