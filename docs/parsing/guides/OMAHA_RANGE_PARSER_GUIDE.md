# Omaha Range Parser API Usage Guide

## Overview

The Omaha Range Parser API extends the Advanced Range Parser to support Omaha (PLO) hand ranges with a syntax inspired by professional poker tools. This implementation supports standard PLO patterns, percentages, and basic operators.

## Implemented Features

### ✅ Phase 1 - Basic Implementation (Complete)

- ✅ **Standard PLO patterns**: `AAxxds`, `JT98r`, `KKxxss`
- ✅ **Specific hands**: `AsKdQhJc`, `AhKdQsJh`
- ✅ **Omaha percentages**: `20%`, `5%`, `10%`
- ✅ **Combinations**: `AAxxds, KKxxds, JT98r`
- ✅ **PLO syntax validation**
- ✅ **Integration** with OmahaHandList
- ✅ **Multi-game Omaha support** (PLO, PLO8, etc.)

### 🚧 Phase 2 - Advanced Operators (In Development)

- 🚧 **Operators**: `+`, `-`, `!`
- 🚧 **Complex expressions**: `20% - AAxx`, `JT98r + JT98ds`
- 🚧 **Extended ranges**: Support for more complex patterns

### 📋 Phase 3 - Optimization (Planned)

- 📋 **Complete Omaha hand rankings**
- 📋 **Optimized performance** for large ranges
- 📋 **Caching** of frequent patterns

## Supported Syntax

### Basic PLO Patterns

```c
// Pairs with wildcards
"AAxx"        // Pair of Aces with any two cards
"KKxx"        // Pair of Kings with any two cards
"QQxx"        // Pair of Queens with any two cards

// Patterns with suitedness
"AAxxds"      // Pair of Aces double-suited
"KKxxss"      // Pair of Kings single-suited
"QQxxr"       // Pair of Queens rainbow (unimplemented)

// Rundowns
"JT98"        // Jack-Ten-Nine-Eight (any suit)
"JT98r"       // Jack-Ten-Nine-Eight rainbow
"JT98ds"      // Jack-Ten-Nine-Eight double-suited
"JT98ss"      // Jack-Ten-Nine-Eight single-suited

// Broadway patterns
"AKQJds"      // Ace-King-Queen-Jack double-suited
"AKQTds"      // Ace-King-Queen-Ten double-suited
"AKJTds"      // Ace-King-Jack-Ten double-suited
```

### Specific Hands

```c
// Fully specified hands
"AsKdQhJc"    // Ace of spades, King of diamonds, Queen of hearts, Jack of clubs
"AhKhQsJs"    // Ace and King of hearts, Queen and Jack of spades
"AdKdQdJd"    // Monotone diamond hand
```

### Percentages

```c
// Top hand percentages
"20%"         // Top 20% of Omaha hands
"10%"         // Top 10% of Omaha hands
"5%"          // Top 5% of Omaha hands
"2.5%"        // Top 2.5% of Omaha hands
```

### Combinations

```c
// Commas to separate patterns
"AAxxds, KKxxds, QQxxds"          // Premium pairs double-suited
"JT98r, JT98ds, JT98ss"           // Rundown in different suits
"AAxx, KKxx, AKQJds"              // Pattern mix
"20%, AAxxds"                     // Percentage plus specific patterns
```

## API Usage

### Basic Example

```c
#include <poker_eval/range/AdvancedRangeParser.h>

int main() {
    // Dead cards (optional)
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    // Parse an Omaha range
    arp_range_t range;
    if (ARP_ParseOmahaRange("AAxxds, KKxxds", dead_cards, game_omaha, &range)) {
        printf("Parsed Omaha range: %zu hands\n", range.count);
        printf("Game type: %s\n", range.is_omaha ? "Omaha" : "Hold'em");
        
        // Convert for use with PLO
        OmahaHandList hand_list;
        if (OmahaHandList_Init(&hand_list, range.count)) {
            ARP_ToOmahaHandList(&range, &hand_list);
            printf("Converted to OmahaHandList: %d hands\n", hand_list.count);
            OmahaHandList_Free(&hand_list);
        }
        
        ARP_FreeRange(&range);
    }
    
    return 0;
}
```

### PLO Syntax Validation

```c
char error_buffer[256];
if (!ARP_ValidateOmahaRangeString("AAxxds, JT98r", error_buffer, sizeof(error_buffer))) {
    printf("PLO syntax error: %s\n", error_buffer);
} else {
    printf("Valid PLO syntax\n");
}
```

### Omaha Percentages

```c
arp_range_t range;
if (ARP_GetOmahaTopPercentage(0.20f, game_omaha, dead_cards, &range)) {
    printf("Top 20%% Omaha: %zu hands\n", range.count);
    ARP_FreeRange(&range);
}
```

### Adding Patterns

```c
arp_range_t range;
ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range);

// Add another pattern
ARP_AddPLOPattern("KKxxds", dead_cards, &range);
printf("Extended range: %zu hands\n", range.count);

ARP_FreeRange(&range);
```

## Practical Examples

### PLO vs PLO Equity Calculation

```c
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/equity/RangeEquity.h>

void calculate_plo_equity() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    // Parse both players' ranges
    arp_range_t range1, range2;
    ARP_ParseOmahaRange("AAxxds, KKxxds", dead_cards, game_omaha, &range1);
    ARP_ParseOmahaRange("JT98r, JT98ds", dead_cards, game_omaha, &range2);
    
    // Convert to PlayerRange for equity calculations
    PlayerRange player_ranges[2];
    ARP_ToPlayerRange(&range1, &player_ranges[0]);
    ARP_ToPlayerRange(&range2, &player_ranges[1]);
    
    // Empty board for preflop
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    
    // Calculate equity (uses existing functions)
    enum_result_t results;
    int matchups = CalculateEquityForRanges(
        game_omaha,
        player_ranges,
        2,
        board,
        dead_cards,
        5,      // 5 board cards to deal
        true,   // Monte Carlo
        10000,  // 10k iterations
        0,
        &results
    );
    
    if (matchups > 0) {
        printf("Premium pairs equity: %.2f%%\n", 
               results.ev[0] / results.nsamples * 100.0);
        printf("Rundowns equity: %.2f%%\n", 
               results.ev[1] / results.nsamples * 100.0);
    }
    
    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);
}
```

### PLO Range Analysis

```c
void analyze_plo_range(const char* range_string) {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    arp_range_t range;
    if (ARP_ParseOmahaRange(range_string, dead_cards, game_omaha, &range)) {
        printf("=== PLO Analysis: %s ===\n", range_string);
        printf("Hand count: %zu\n", range.count);
        
        if (range.is_percentage) {
            printf("Percentage: %.2f%%\n", range.percentage_used * 100.0f);
        }
        
        // Convert for PLO analysis
        OmahaHandList hand_list;
        if (OmahaHandList_Init(&hand_list, range.count)) {
            ARP_ToOmahaHandList(&range, &hand_list);
            
            // Analyze hand types (requires PLONomenclature)
            printf("Hands converted for PLO analysis\n");
            
            OmahaHandList_Free(&hand_list);
        }
        
        ARP_FreeRange(&range);
    }
}
```

### Range Comparison

```c
void compare_ranges() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    const char* ranges[] = {
        "AAxxds",
        "20%",
        "JT98r, JT98ds",
        "AAxx, KKxx, QQxx",
        NULL
    };
    
    printf("=== PLO Range Comparison ===\n");
    
    for (int i = 0; ranges[i] != NULL; i++) {
        arp_range_t range;
        if (ARP_ParseOmahaRange(ranges[i], dead_cards, game_omaha, &range)) {
            printf("%-20s: %zu hands", ranges[i], range.count);
            if (range.is_percentage) {
                printf(" (%.1f%%)", range.percentage_used * 100.0f);
            }
            printf("\n");
            ARP_FreeRange(&range);
        }
    }
}
```

## Supported Game Types

The API supports all Omaha variants:

```c
// Supported Omaha game types
enum_game_t omaha_games[] = {
    game_omaha,     // PLO High
    game_omaha8,    // PLO Hi/Lo 8-or-better
    game_omaha5,    // 5-card PLO
    game_omaha6,    // 6-card PLO
    game_omaha85    // 5-card PLO Hi/Lo
};

// Usage
for (int i = 0; i < 5; i++) {
    arp_range_t range;
    if (ARP_ParseOmahaRange("AAxxds", dead_cards, omaha_games[i], &range)) {
        printf("Game %d: %zu hands\n", omaha_games[i], range.count);
        ARP_FreeRange(&range);
    }
}
```

## Error Handling

### Return Codes
- `1`: Success
- `0`: Failure

### PLO-Specific Error Messages

```c
char error_buffer[256];
if (!ARP_ValidateOmahaRangeString("INVALID_PLO", error_buffer, sizeof(error_buffer))) {
    printf("PLO Error: %s\n", error_buffer);
}
```

### Common PLO Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `Invalid PLO pattern` | Unrecognized pattern | Check PLO syntax |
| `Failed to parse PLO pattern` | Error in PLOIntegration | Check dependencies |
| `Non-Omaha game type` | Non-Omaha game | Use game_omaha, etc. |
| `Failed to expand Omaha percentage` | Percentage error | Verify 0.0 < % <= 1.0 |

## Performance

### Complexity for Omaha
- **PLO Parsing**: O(n) where n = pattern length
- **Generation**: O(m) where m = number of generated hands
- **Memory**: O(m) to store hands

### PLO Optimizations
- Using PLOIntegration for efficient generation
- Frequent pattern cache (future)
- Optimized dynamic allocation

## Current Limitations

1. **Operators**: +, -, ! not implemented (Phase 2)
2. **Full rankings**: Uses simplified patterns for %
3. **Advanced patterns**: Some complex PLO patterns not supported
4. **Performance**: Not optimized for very large ranges

## Roadmap

### Phase 2 (3-5 days)
- [ ] Operator implementation (`+`, `-`, `!`)
- [ ] Support for complex expressions
- [ ] Percentage improvement with full rankings
- [ ] Performance testing

### Phase 3 (2-3 days)
- [ ] Memory and speed optimization
- [ ] Intelligent pattern caching
- [ ] Support for advanced PLO patterns
- [ ] Complete documentation

## Ecosystem Integration

### Compatibility
- ✅ **PLONomenclature**: Uses existing PLO structures
- ✅ **omaha_distributions.h**: Full integration
- ✅ **RangeEquity**: Compatible with equity calculations
- ✅ **PLOIntegration**: Uses generation functions

### Future Extensions
- Support for new Omaha variants
- GPU integration for large ranges
- Advanced range manipulation API
- Standard format range export/import

## Advanced Usage Examples

### Progressive Range Building

```c
// Build a range progressively
arp_range_t range;
ARP_ParseOmahaRange("", dead_cards, game_omaha, &range); // Empty range

// Add patterns one by one
ARP_AddPLOPattern("AAxxds", dead_cards, &range);
ARP_AddPLOPattern("KKxxds", dead_cards, &range);
ARP_AddPLOPattern("QQxxds", dead_cards, &range);

printf("Built range: %zu hands\n", range.count);
ARP_FreeRange(&range);
```

### Real-time Validation

```c
// For user interface
bool validate_user_input(const char* input) {
    char error[256];
    return ARP_ValidateOmahaRangeString(input, error, sizeof(error));
}
```

This implementation provides a solid foundation for parsing Omaha ranges with a professional syntax, ready for extension with advanced operators in Phase 2.