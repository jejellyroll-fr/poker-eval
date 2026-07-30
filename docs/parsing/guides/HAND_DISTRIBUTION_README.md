# Hand Distribution Feature

This branch (`feat/omaha-distribution-core`) adds hand distribution functionality to poker-eval, allowing you to work with hand ranges in both Holdem and Omaha.

## Build Status

The project builds successfully with the hand distribution feature. Some minor issues with equity calculation aggregation need to be addressed, but the core distribution functionality works correctly.

## Features Added

### 1. Holdem Hand Distribution (existing, enhanced)
- Parse hand ranges like "AK", "AKs", "AKo", "AA"
- Generate all possible hand combinations from a range
- Handle dead cards correctly

### 2. Omaha Hand Distribution (new)
- Parse Omaha hand patterns like:
  - "AAxx" - pair of aces with any two cards
  - "AAxxds" - double-suited aces
  - "AKQJds" - double-suited broadway
  - "AsKhQdJc" - specific hand
  - "AAAx" - trip aces with any card
- Support for suit properties:
  - ds (double-suited)
  - ss (single-suited)
  - ts (triple-suited)
  - qs (quad-suited/monotone)
  - r/rb (rainbow)

### 3. Files Modified/Added

**Modified:**
- `src/distributions/omaha_distributions.c` - Fixed `STD_DECK_N_CARDS` → `StdDeck_N_CARDS`
- `src/distributions/stud_distributions.c` - Fixed `STD_DECK_N_CARDS` → `StdDeck_N_CARDS`
- `src/equity/RangeEquity.c` - Fixed `ENUMORD_DEFAULT_MODE` → `enum_ordering_mode_hi`
- `include/poker_eval/distributions/omaha_distributions.h` - Uncommented function declarations
- `CMakeLists.txt` - Added new tests and examples

**Added:**
- `tests/test_hand_distribution.c` - Comprehensive test of hand distributions
- `tests/test_simple_equity.c` - Simple equity calculation test
- `examples/hand_distribution_example.c` - Example usage of hand distributions

## Test Results

### Holdem Distribution Tests
```
AKs generates 4 hands (all suited combinations)
AKo generates 12 hands (all offsuit combinations)
AK generates 16 hands (all combinations)
AA generates 6 hands (all pocket pairs)
Dead cards are properly handled
```

### Omaha Distribution Tests
```
AAxx generates 2000 hands (limited by MAX_OMAHA_COMBOS)
AAxxds generates 864 hands (double-suited only)
AsKhQdJc generates 1 hand (specific hand)
AKQJds generates 36 hands (double-suited broadway combinations)
AAAx generates 192 hands (C(4,3) * 48 = 192)
```

## Known Issues

1. **Equity Calculation**: The equity aggregation in the examples shows incorrect values for Holdem. This appears to be an issue with how results are averaged across multiple matchups, not with the distribution functionality itself.

2. **MAX_OMAHA_COMBOS Limit**: Currently set to 2000, which limits some broader ranges like "AAxx". This could be increased if needed.

3. **Warning Messages**: Some format string warnings in HoldemAgnosticHand.c that don't affect functionality.

## Usage Examples

### Holdem
```c
HandList hands;
HoldemAgnosticHand_Instantiate("AKs", "", &hands);
// hands now contains all suited AK combinations
```

### Omaha
```c
OmahaHandQuery query;
OmahaHandList hands;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

OmahaHand_Parse("AAxxds", &query);
OmahaHandList_Init(&hands, 1000);
OmahaHand_Instantiate(&query, dead, &hands);
// hands now contains all double-suited AA combinations
```

## Building

```bash
mkdir build
cd build
cmake ..
make

# Run tests
./test_hand_distribution
./test_simple_equity

# Run examples
./hand_distribution_example
```

## Next Steps

1. Fix equity calculation aggregation
2. Add more comprehensive tests
3. Optimize performance for large ranges
4. Add Python bindings for the new Omaha functionality
5. Document the API more thoroughly