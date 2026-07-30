# Advanced Range Parser API Usage Guide

## Overview

The Advanced Range Parser API allows parsing and manipulating poker hand ranges using a standard and intuitive syntax. This API supports notations commonly used in modern poker software.

## Features

### ✅ Implemented (Phase 1 & 2)
- ✅ **Pocket pairs**: `AA`, `KK`, `AA-TT`
- ✅ **Suited/offsuit hands**: `AKs`, `AKo`, `AK`
- ✅ **Specific hands**: `AsKh`, `AdQd`
- ✅ **Combinations**: `AA, KK, AKs`
- ✅ **Percentages**: `20%`, `5.5%`
- ✅ **Syntax validation**
- ✅ **Conversion** to PlayerRange
- ✅ **Dead card handling**

### ✅ Implemented (Phase 3 - Optimizations)
- ✅ **Percentage cache**: 40x+ speedup for repeated queries
- ✅ **Memory optimization**: Intelligent capacity estimation
- ✅ **Hash table**: O(1) duplicate detection (O(n) total vs O(n²))

### ✅ Implemented (Phase 4 - Advanced API)
- ✅ **Extended ranges**: `AK-AJ`, `AKs-AJs`
- ✅ **Arithmetic operators**: `+` (union), `-` (difference), `!` (exclusion)
- ✅ **Stud support**: `(AA)K`, `(ss)Ks`
- ✅ **Detailed error messages** with `ARP_ParseRangeWithError`

### ✅ Implemented (Phase 5 - Final Polish)
- ✅ **Complete utility API**: `CountCombinations`, `CloneRange`, `RangesEqual`, `IntersectRanges`, `ContainsHand`
- ✅ **Import/Export**: Simple text format for saving/loading ranges
- ✅ **Exhaustive tests**: 19+ tests covering cache, utils, and parsing
- ✅ **Benchmarks**: Full performance measurement suite

## Supported Syntax

See the detailed document [RANGE_SYNTAX.md](./RANGE_SYNTAX.md) for the complete specification.

### Basic Hands

```c
// Pocket pairs
"AA"          // Pair of Aces (6 combinations)
"KK"          // Pair of Kings (6 combinations)

// Suited hands
"AKs"         // Ace-King suited (4 combinations)

// Offsuit hands
"AKo"         // Ace-King offsuit (12 combinations)

// Mixed hands (suited + offsuit)
"AK"          // Ace-King (16 combinations)
```

### Extended Ranges

```c
// Pair ranges
"AA-TT"       // Pocket pairs Aces to Tens (30 combinations)

// Non-pair hand ranges
"AK-AJ"       // AK, AQ, AJ (48 combinations)
"AKs-AJs"     // AKs, AQs, AJs (12 combinations)

// Operators
"AA-TT + AK"  // Pairs + AK
"20% - AA"    // Top 20% excluding Aces
"!AA"         // Everything except Aces
```

### Percentages

```c
"20%"         // Top 20% of hands (Hold'em)
"5%"          // Top 5%
```

## API Usage

### Basic Parsing

```c
#include <poker_eval/range/AdvancedRangeParser.h>

arp_range_t range;
StdDeck_CardMask dead_cards;
StdDeck_CardMask_RESET(dead_cards);

if (ARP_ParseRange("AA-TT, AKs", dead_cards, game_holdem, &range)) {
    printf("Range: %zu hands\n", range.count);
    ARP_FreeRange(&range);
}
```

### Utility Functions (Phase 5)

The API now offers powerful tools for manipulating ranges without complex parsing:

```c
// Count hands without generating the full range
size_t count = ARP_CountCombinations("AA-TT", dead_cards, game_holdem);

// Check if a hand is in the range
bool has_aces = ARP_ContainsHand(&range, aces_mask);

// Intersection of two ranges
arp_range_t intersection;
ARP_IntersectRanges(&range1, &range2, &intersection);

// Compare two ranges
bool equal = ARP_RangesEqual(&range1, &range2);

// Clone a range
arp_range_t clone;
ARP_CloneRange(&original, &clone);
```

### Detailed Error Handling

```c
arp_error_details_t error;
if (!ARP_ParseRangeWithError("invalid", dead, game_holdem, &range, &error)) {
    char buffer[256];
    ARP_FormatError(&error, buffer, sizeof(buffer));
    printf("Error: %s\n", buffer);
}
```

## Performance

### Percentage Cache

The parser uses a thread-safe cache for percentage queries ("20%", "5%").

- **First call**: ~10-20µs (initial calculation)
- **Subsequent calls**: ~0.2-0.5µs (memory copy)
- **Speedup**: ~40x

See [PERFORMANCE_GUIDE.md](../../optimization/guides/PERFORMANCE_GUIDE.md) for more details.

### Memory Optimization

- Exact allocation for simple ranges (pairs, specific hands)
- Hash tables for large ranges (≥50 hands) for O(1) duplicate prevention

## Benchmarks

Typical results on a modern machine:

| Scenario | Time (µs) | Hands/sec |
|----------|-----------|-----------|
| Single pair (AA) | 0.26 | 22M |
| Top 20% (Cache) | 0.42 | 516M |
| Top 50% (Cache) | 0.44 | 1.1G |

## Multi-Game Support

- **Hold'em**: Full support
- **Omaha (PLO)**: Partial support (patterns `AAxxds`, basic percentages)
- **Stud**: Support for `(AA)K` patterns
