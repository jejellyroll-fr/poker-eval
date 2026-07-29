# Range of Hands Syntax Reference

This document describes the range syntax supported by the poker-eval Advanced Range Parser (ARP).

## Table of Contents

1. [Generic Range Syntax](#generic-range-syntax)
2. [Hold'em Range Syntax](#holdem-range-syntax)
3. [Omaha Range Syntax](#omaha-range-syntax)
4. [Stud Range Syntax](#stud-range-syntax)
5. [Performance Considerations](#performance-considerations)
6. [Utility Functions](#utility-functions)
7. [API Reference](#api-reference)

---

## Generic Range Syntax

The Advanced Range Parser provides a unified syntax for describing ranges of poker hands across multiple game variants.

### Supported Games

- Texas Hold'em (game_holdem)
- Omaha / Omaha Hi-Lo (game_omaha, game_omaha8)
- 5-Card Omaha (game_omaha5, game_omaha5_8)
- 6-Card Omaha (game_omaha6, game_omaha6_8)
- 7-Card Stud (game_7stud, game_7stud8)
- Razz (game_razz)

### Basic Elements

#### Ranks and Suits

- **Ranks**: `A K Q J T 9 8 7 6 5 4 3 2` (Ace is always high)
- **Suits**: `s h d c` (spades, hearts, diamonds, clubs)
- **Wildcard**: `x` represents any card

#### Suit Variables

- `x, y, z, w` - suit variables that bind to different suits
- Example: `xx` means "two cards of the same suit"
- Example: `xy` means "two cards of different suits"

### Percent Ranges

Poker-eval includes hand strength rankings for generating percentage-based ranges:

```
5%        Top 5% of hands
20%       Top 20% of hands
5%-10%    Hands between top 5% and top 10%
```

**Implementation Note**: Percentage ranges automatically set the `is_percentage` flag and store the value in `percentage_used`.

### Combining Ranges

Three operators allow combining multiple ranges:

| Operator | Name | Description | Example |
|----------|------|-------------|---------|
| `,` | Union (OR) | All hands from either range | `AA, KK` |
| `+` | Addition | Equivalent to union | `AA + KK` |
| `-` | Subtraction | Hands in first range but not second | `20% - AA` |
| `!` | Exclusion | Exclude specific hands | `!AA` |

**Operator Precedence** (highest to lowest):
1. Parentheses `()`
2. Unary NOT `!`
3. Addition `+` and Subtraction `-` (left-to-right)
4. Comma `,` (lowest precedence)

---

## Hold'em Range Syntax

### Specific Hands

```
AsKh      Ace of spades and king of hearts (specific cards)
AA        Any pair of aces
KK        Any pair of kings
AK        Ace-king (both suited and offsuit)
```

### Suited and Offsuit

```
AKs       Ace-king suited (any suit)
AKo       Ace-king offsuit
QJs       Queen-jack suited
```

**Implementation Detail**:
- `AKs` creates token type `ARP_TOKEN_SUITED`
- `AKo` creates token type `ARP_TOKEN_OFFSUIT`
- `AK` creates token type `ARP_TOKEN_BOTH`

### Pocket Pair Ranges

```
AA-KK     Pairs from aces to kings (inclusive)
AA-TT     Pairs from aces to tens
22-88     Small to medium pairs
```

**How it works**: The tokenizer detects the pattern `[Rank][Rank]-[Rank][Rank]` where both pairs of ranks match, creating a `ARP_TOKEN_PAIR_RANGE` token with `high_rank` and `low_rank`.

### Complex Combinations

```
AA-KK, AKs              Premium pairs or AK suited
AA, KK, QQ              Top three pairs
AA-TT + AK-AJ           Premium pairs plus big aces
20% - AA                Top 20% excluding pocket aces
```

### Hold'em Examples

| Range | Meaning | Hand Count |
|-------|---------|------------|
| `AA` | Pocket aces | 6 |
| `AK` | Ace-king (any) | 16 |
| `AKs` | Ace-king suited | 4 |
| `AKo` | Ace-king offsuit | 12 |
| `AA-KK` | Aces or kings | 12 |
| `AA-TT` | Big pairs | 30 |
| `5%` | Top 5% of hands | ~50 hands |

---

## Omaha Range Syntax

Omaha ranges support 4-card (PLO) and 5/6-card variants with suit coordination.

### PLO Pattern Syntax

#### Basic Patterns

```
AAxx      Pocket aces with any two cards
KKxx      Pocket kings with any two cards
AKxx      Ace-king with any two cards
JT98      Connected cards (rundown)
```

#### Suit Indicators

```
AAxxds    Pocket aces double-suited
AAxxss    Pocket aces single-suited (three suits)
JT98r     Jack-ten-nine-eight rainbow (four suits)
```

**Suit Suffixes**:
- `ds` - double-suited (two suits, two cards each)
- `ss` - single-suited (three suits)
- `r` - rainbow (four different suits)

### Specific 4-Card Hands

```
AsKdQhJc  Specific four cards with exact suits
AhKhQdJd  Ace-king of hearts, queen-jack of diamonds
```

**Detection Rule**: 8-character strings with alternating rank-suit pattern are recognized as specific 4-card hands.

### Percentage Ranges

```
5%        Top 5% of Omaha hands
20%       Top 20% of Omaha hands
5%-10%    Hands between 5th and 10th percentile
```

### Complex Omaha Ranges

```
AAxxds, KKxxds          Double-suited aces or kings
AAxx, JT98r             Aces or connected rundown
AAxxds + KKxxds         Union of two patterns
```

### Omaha Examples

| Range | Meaning | Approximate Hand Count |
|-------|---------|----------------------|
| `AAxx` | Any hand with pocket aces | 6,768 |
| `AAxxds` | Pocket aces double-suited | 864 |
| `AAxxss` | Pocket aces single-suited | 4,248 |
| `JT98` | Jack-ten-nine-eight (any suits) | 256 |
| `JT98r` | Jack-ten-nine-eight rainbow | 24 |
| `AsKdQhJc` | Specific hand | 1 |

### PLO Pattern Detection Algorithm

The parser uses `arp_is_plo_pattern()` to detect PLO patterns:

1. **Card Count**: Patterns with 4+ cards (ranks or 'x') are PLO candidates
2. **Operator Check**: Patterns containing Hold'em operators (`-`, `,`, `+`, `!`) are rejected
3. **Specific Hands**: 8-character rank-suit alternating patterns are recognized

### PLO Categories

The parser also understands the 21-category nomenclature from `plo_nomenclature`.  
Use the `cat:` or `category:` prefix (case-insensitive, accepts `-` or `_`) to expand predefined clusters without writing verbose patterns.

- `cat:aa_ds` → all AA double-suited holdings (same as `AAxxds`)
- `category:unpaired-ss` → single-suited unpaired hands
- `CAT:BROADWAY_RB{50%}` → rainbow Broadway category with a 50% weight
- Dead cards are respected: `cat:aa_ss` automatically excludes blocked suits

Aliases like `AA-DS`, `aces_ds`, or uppercase spellings are accepted. Weighted categories can be combined with the usual operators (`+`, `-`, `!`, `,`) and parentheses.
4. **Suit Suffixes**: Patterns ending in `ds`, `ss`, or `r` are PLO patterns

---

## Stud Range Syntax

### Basic Stud Patterns

```
(AA)K           Aces in the hole with king showing
(AK)Q           Ace-king in hole with queen showing
(ss)Ks          Two spades in hole with king of spades showing
```

**Note**: Parentheses indicate hole cards (cards 1-2), followed by upcards.

### Card Order in Stud

Card order is significant for third street and beyond. The first two hole cards are order-independent.

```
KsJhTd     King-jack in hole, ten showing (same as JhKsTd)
KsJhTd9c   King-jack hole, ten-nine showing (order matters for upcards)
```

### Stud Examples

| Range | Meaning |
|-------|---------|
| `(AA)K` | Split aces with king door card |
| `(KK)x` | Buried kings with any upcard |
| `(ss)Ks` | Three-flush with king showing |
| `(xx)RR` | Any hole cards with pair showing |

---

## Performance Considerations

The Advanced Range Parser includes several optimizations for production use:

### Percentage Caching

Percentage-based ranges (e.g., "20%", "5%") are automatically cached with a thread-safe LRU cache:

```c
// First parse - computes from hand rankings
arp_range_t range1;
ARP_ParseRange("20%", dead, game_holdem, &range1);  // ~10-20 µs

// Second parse - cache hit
arp_range_t range2;
ARP_ParseRange("20%", dead, game_holdem, &range2);  // ~0.3 µs (40x faster!)
```

**Cache characteristics**:
- **Size**: 32 entries
- **TTL**: 1 hour (3600 seconds)
- **Thread-safe**: Yes (pthread_mutex)
- **Eviction**: LRU (Least Recently Used)
- **Disabled when**: Dead cards present

**Cache control**:
```c
ARP_InitCache();        // Initialize (optional, auto-init)
ARP_ClearCache();       // Clear all entries
ARP_GetCacheStats(&s);  // Get statistics
```

### Memory Optimization

Range allocation is optimized based on input type:

| Range Type | Allocation Strategy | Example |
|------------|---------------------|---------|
| Pocket pairs | Exact (6 combos/pair) | `AA` → 6 slots |
| Hand ranges | Exact calculation | `AKs` → 4 slots, `AKo` → 12 slots |
| Large ranges | Pre-estimated with growth | `AA-22` → 78 slots |
| Percentages | Estimated by % value | `20%` → 200-250 slots |

**Benefits**:
- Reduces memory overhead by 20-30%
- Fewer realloc() calls
- Better cache locality

### Hash Table Duplicate Detection

For large ranges (≥50 hands), hash tables provide O(1) lookup:

```c
// Small range (<50 hands): Linear search O(n)
"AA, KK, QQ"  // 18 hands - linear search

// Large range (≥50 hands): Hash table O(1)
"AA-22"       // 78 hands - hash table enabled
"20%"         // 216 hands - hash table enabled
```

**Hash table characteristics**:
- **Size**: 2048 buckets
- **Collision**: Linear probing (max 20 steps)
- **Complexity**: O(n) total vs O(n²) without hash

### Benchmarks

Typical performance on modern hardware (Intel i7, 3.5GHz):

| Operation | Time (µs) | Hands/sec | Notes |
|-----------|-----------|-----------|-------|
| Single pair (`AA`) | 0.34 | 17.7M | Fast path |
| Suited hand (`AKs`) | 0.35 | 11.3M | |
| Range (`AA-TT`) | 0.45 | 66.7M | 30 hands |
| Top 1% | 0.37 | 32.0M | 12 hands, cached |
| Top 20% (cached) | 0.79 | 272.7M | 216 hands |
| Top 50% (cached) | 0.90 | 562.5M | 506 hands |

**Cache impact**:
- Cold cache: ~10-20 µs
- Warm cache: ~0.3-0.8 µs
- **Speedup**: 10-40x

See [Performance Guide](guides/PERFORMANCE_GUIDE.md) for detailed analysis.

---

## Utility Functions

The Advanced Range Parser provides utility functions for common operations without full parsing.

### Quick Operations

#### Count combinations without generating

Useful for UI display or validation:

```c
#include <poker_eval/distributions/AdvancedRangeParser.h>

// Count hands in a range without allocating
size_t count = ARP_CountCombinations("AA-TT + AK-AJ", dead_cards, game_holdem);
printf("Range has %zu combinations\n", count);  // 78 combos
```

**Performance**: Very fast (~0.5 µs), only counts without allocating hands.

#### Check if specific hand in range

Efficient membership test:

```c
// Create specific hand
StdDeck_CardMask hand;
StdDeck_CardMask_RESET(hand);
StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));

// Parse range
arp_range_t range;
ARP_ParseRange("AA-TT", dead_cards, game_holdem, &range);

// Check membership
if (ARP_ContainsHand(&range, hand)) {
    printf("AsAh is in range\n");
}

ARP_FreeRange(&range);
```

**Complexity**: O(1) if hash table enabled (range ≥50 hands), O(n) otherwise.

### Range Manipulation

#### Clone a range

Deep copy for independent modifications:

```c
arp_range_t original, clone;
ARP_ParseRange("AA, KK, AKs", dead_cards, game_holdem, &original);

// Clone creates independent copy
ARP_CloneRange(&original, &clone);

// Modify clone without affecting original
// ...

ARP_FreeRange(&original);
ARP_FreeRange(&clone);
```

#### Compare ranges

Check if two ranges contain the same hands:

```c
arp_range_t range1, range2;
ARP_ParseRange("AA, KK", dead, game_holdem, &range1);
ARP_ParseRange("KK, AA", dead, game_holdem, &range2);

if (ARP_RangesEqual(&range1, &range2)) {
    printf("Ranges are equal (order doesn't matter)\n");
}

ARP_FreeRange(&range1);
ARP_FreeRange(&range2);
```

#### Intersect ranges

Find common hands between ranges:

```c
arp_range_t range1, range2, intersection;

ARP_ParseRange("AA-TT", dead, game_holdem, &range1);      // High pairs
ARP_ParseRange("QQ-22", dead, game_holdem, &range2);      // Medium-low pairs

ARP_IntersectRanges(&range1, &range2, &intersection);
// intersection contains: QQ, JJ, TT (overlap)

printf("Intersection has %zu hands\n", intersection.count);

ARP_FreeRange(&range1);
ARP_FreeRange(&range2);
ARP_FreeRange(&intersection);
```

### Import/Export

#### Export range to file

Save range in human-readable format:

```c
arp_range_t range;
ARP_ParseRange("AA-TT, AKs", dead_cards, game_holdem, &range);

// Export to file
FILE *f = fopen("my_range.txt", "w");
int exported = ARP_ExportRange(&range, f, game_holdem);
fclose(f);

printf("Exported %d hands\n", exported);

ARP_FreeRange(&range);
```

**Format**:
```
# Range Export
# Game: 1
# Hands: 34
# Total Weight: 34.000000
# Has Weights: no

AsAh
AsAd
AsAc
...
```

#### Import range from file

⚠️ **Note**: Import is currently a placeholder and needs implementation for production use.

```c
arp_range_t range;

FILE *f = fopen("my_range.txt", "r");
int success = ARP_ImportRange(f, &range);
fclose(f);

if (success) {
    printf("Imported %zu hands\n", range.count);
    ARP_FreeRange(&range);
} else {
    printf("Import failed or not implemented\n");
}
```

### Complete API Reference

| Function | Purpose | Complexity |
|----------|---------|------------|
| `ARP_CountCombinations` | Count without generating | O(parsing) |
| `ARP_CloneRange` | Deep copy range | O(n) |
| `ARP_RangesEqual` | Compare ranges | O(n²) or O(n log n) |
| `ARP_IntersectRanges` | Find common hands | O(n·m) |
| `ARP_ContainsHand` | Check membership | O(1) or O(n) |
| `ARP_ExportRange` | Save to file | O(n) |
| `ARP_ImportRange` | Load from file | O(n) ⚠️ |

⚠️ = Currently incomplete, use with caution

See the [Advanced Range Parser Guide](ADVANCED_RANGE_PARSER_GUIDE.md) for complete documentation.

---

## API Reference

### Core Functions

#### ARP_ParseRange

```c
int ARP_ParseRange(
    const char *range_string,
    StdDeck_CardMask dead_cards,
    enum_game_t game_type,
    arp_range_t *result
);
```

Parse a generic range string for any game type.

**Parameters**:
- `range_string`: Range expression (e.g., "AA-KK, AKs")
- `dead_cards`: Cards to exclude from generation
- `game_type`: Game variant (game_holdem, game_omaha, etc.)
- `result`: Output range structure

**Returns**: 1 on success, 0 on failure

**Example**:
```c
arp_range_t range;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

if (ARP_ParseRange("AA-TT, AKs", dead, game_holdem, &range)) {
    printf("Generated %zu hands\n", range.count);
    ARP_FreeRange(&range);
}
```

#### ARP_ParseOmahaRange

```c
int ARP_ParseOmahaRange(
    const char *range_string,
    StdDeck_CardMask dead_cards,
    enum_game_t game_type,
    arp_range_t *result
);
```

Parse an Omaha-specific range string with PLO pattern support.

**Parameters**: Same as ARP_ParseRange
**Returns**: 1 on success, 0 on failure
**Note**: Validates that game_type is an Omaha variant

**Example**:
```c
arp_range_t range;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

if (ARP_ParseOmahaRange("AAxxds, JT98r", dead, game_omaha, &range)) {
    printf("Generated %zu PLO hands\n", range.count);
    ARP_FreeRange(&range);
}
```

#### ARP_GetTopPercentage

```c
int ARP_GetTopPercentage(
    float percentage,
    enum_game_t game_type,
    StdDeck_CardMask dead_cards,
    arp_range_t *result
);
```

Generate top N% of hands based on precomputed rankings.

**Parameters**:
- `percentage`: Percentage value (0.0 to 1.0)
- `game_type`: Game variant
- `dead_cards`: Cards to exclude
- `result`: Output range

**Returns**: 1 on success, 0 on failure

**Example**:
```c
arp_range_t range;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

if (ARP_GetTopPercentage(0.05f, game_holdem, dead, &range)) {
    printf("Top 5%% contains %zu hands\n", range.count);
    ARP_FreeRange(&range);
}
```

### Data Structures

#### arp_range_t

```c
typedef struct {
    StdDeck_CardMask *hands;  // Array of hand masks
    double *weights;          // Optional weights per hand (NULL if uniform)
    size_t count;             // Number of hands in the range
    size_t capacity;          // Allocated capacity
    double total_weight;      // Sum of weights
    bool has_weights;         // True if non-uniform weights are in use
    float percentage_used;    // If percentage range, value stored here
    bool is_percentage;       // True if defined by percentage
    enum_game_t game_type;    // Game type for this range
    bool is_omaha;            // True if Omaha variant
} arp_range_t;
```

#### arp_token_type_t

```c
typedef enum {
    ARP_TOKEN_UNKNOWN = 0,
    ARP_TOKEN_PAIR_RANGE,      // AA-TT
    ARP_TOKEN_SUITED,          // AKs
    ARP_TOKEN_OFFSUIT,         // AKo
    ARP_TOKEN_BOTH,            // AK (both suited and offsuit)
    ARP_TOKEN_PERCENTAGE,      // 20%
    ARP_TOKEN_SPECIFIC_HAND,   // AsKh
    ARP_TOKEN_OPERATION,       // +, -, !
    ARP_TOKEN_COMMA,           // ,
    ARP_TOKEN_PLO_PATTERN,     // AAxxds, JT98r
    ARP_TOKEN_END
} arp_token_type_t;
```

### Pondérations et exclusions ciblées

Chaque plage peut être pondérée via un suffixe `{...}`. Les pondérations peuvent être exprimées en valeur absolue (`0.5`) ou en pourcentage (`50%`).

```
AKo{0.5}, AKs{50%}
AA{75%} + KK{25%}
```

Les pondérations sont automatiquement normalisées lors de la conversion en `PlayerRange` (`total_weight` et `weights[]`).

L'opérateur `!` appliqué dans une expression retire uniquement les combos déjà ajoutés. Il est ainsi possible d'exclure précisément certaines combinaisons :

```
AA, !AsAh        # Conserve 5 combos d'AA
(AA,KK) + !AKs   # Ajoute AA/KK puis retire AKs
```

Ces fonctionnalités sont disponibles dans les API C et Python (notamment pour `CalculateMultiwayEquity`).

Consultez `docs/guides/MULTIWAY_EQUITY_GUIDE.md` pour un exemple complet.


### Validation Functions

#### ARP_ValidateRangeString

```c
int ARP_ValidateRangeString(
    const char *range_string,
    char *error_buffer,
    size_t error_buffer_size
);
```

Validate a range string without fully parsing it.

**Returns**: 1 if valid, 0 if invalid

#### ARP_ValidateOmahaRangeString

```c
int ARP_ValidateOmahaRangeString(
    const char *range_string,
    char *error_buffer,
    size_t error_buffer_size
);
```

Validate an Omaha range string.

**Returns**: 1 if valid, 0 if invalid

### Conversion Functions

#### ARP_ToPlayerRange

```c
int ARP_ToPlayerRange(
    const arp_range_t *arp_range,
    PlayerRange *player_range
);
```

Convert an arp_range_t to a PlayerRange for equity calculations.

**Returns**: 1 on success, 0 on failure

#### ARP_ToOmahaHandList

```c
int ARP_ToOmahaHandList(
    const arp_range_t *arp_range,
    OmahaHandList *omaha_list
);
```

Convert an arp_range_t to an OmahaHandList for PLO functions.

**Returns**: 1 on success, 0 on failure

### Utility Functions

#### ARP_FreeRange

```c
void ARP_FreeRange(arp_range_t *range);
```

Free resources allocated for a range.

#### ARP_RangeToString

```c
int ARP_RangeToString(
    const arp_range_t *range,
    char *buffer,
    size_t buffer_size
);
```

Get string representation of a range.

**Returns**: Number of characters written

#### ARP_GetRangePercentage

```c
float ARP_GetRangePercentage(
    const arp_range_t *range,
    enum_game_t game_type
);
```

Calculate what percentage of all possible hands this range represents.

**Returns**: Percentage (0.0 to 1.0)

---

## Implementation Notes

### Tokenization Process

1. **Input Parsing**: Range string is tokenized character by character
2. **PLO Detection**: Multi-card patterns are checked via `arp_is_plo_pattern()`
3. **Token Creation**: Each recognized pattern creates a typed token
4. **Expression Tree**: Tokens are parsed into an expression tree respecting operator precedence

### Expression Evaluation

1. **Tree Traversal**: Expression tree is evaluated depth-first
2. **Leaf Nodes**: Generate hands using pattern-specific algorithms
3. **Operations**: Union, subtraction, and exclusion combine ranges
4. **Result**: Final range contains all matching hands minus dead cards

### Performance Considerations

- **Caching**: Percentage ranges can be pre-computed and cached
- **Dead Cards**: Filtering happens during generation, not post-processing
- **Memory**: Ranges use dynamically-sized arrays with capacity management
- **Complexity**: O(n) for generation, O(n log n) for operations with large ranges

---

## Error Handling

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| "Invalid rank" | Unknown rank character | Use A, K, Q, J, T, 9-2 |
| "Invalid suit" | Unknown suit character | Use s, h, d, c |
| "Incomplete range" | Missing end of range | Check AA-KK has both pairs |
| "Unexpected token" | Invalid syntax | Check operator precedence |
| "Not an Omaha game" | Wrong game type | Use game_omaha/game_omaha8 |

### Validation

Always validate range strings before parsing in production:

```c
char error[256];
if (!ARP_ValidateRangeString("AA-KK, AKs", error, sizeof(error))) {
    fprintf(stderr, "Invalid range: %s\n", error);
    return;
}
```

---

## Examples Collection

### Hold'em Opening Ranges

```c
// Ultra-tight (UTG)
"AA-QQ, AKs"

// Tight (MP)
"AA-99, AK, AQs, AJs"

// Standard (CO)
"AA-77, AK-AT, KQ-KT, QJ, JT, T9s, 98s"

// Wide (BTN)
"AA-22, AK-A2, KQ-K9, QJ-Q9, JT-J9, T9-T8, 98-96, 87-85, 76-74, 65-63, 54s, 43s"

// 3-bet range (IP)
"AA-TT, AK-AQ, KQs"
```

### Omaha Starting Hands

```c
// Premium PLO
"AAxxds, AAxxss, KKxxds, QQxxds, JJxxds"

// Connected rundowns
"JT98ds, T987ds, 9876ds, JT98r, T987r"

// Broadway hands
"AKQJds, AKQTds, AKJTds, AQJTds, KQJTds"

// Suited aces
"AxKxQx, AxKxJx, AxQxJx, AxJxTx"

// Top 20% for loose game
"20%"

// Top 10% excluding pure aces (need wrap/connectivity)
"10% - AAxx"
```

### Tournament Ranges

```c
// Push range (10BB)
"AA-22, AK-A2, KQ-K2, QJ-Q2, JT-J5, T9-T6, 98-96, 87-85, 76-74, 65, 54s"

// Call range (15BB vs push)
"AA-77, AK-AJ, KQs"

// Bubble play (aggressive)
"30%"
```

---

## Testing

### Test Coverage

Current test suite (see `tests/test_advanced_range_parser.c` and `tests/test_omaha_range_parser.c`):

- ✅ Pocket pair ranges (AA-KK, AA-TT)
- ✅ Suited/offsuit notation (AKs, AKo)
- ✅ Specific hands (AsKh, AdQd)
- ✅ Comma-separated ranges (AA, KK, QQ)
- ✅ Percentage ranges (5%, 20%, 5.5%)
- ✅ Operator expressions (AA + KK, AA-TT - QQ)
- ✅ Dead card filtering
- ✅ PlayerRange conversion
- ✅ PLO pattern recognition (AAxxds, JT98r)
- ✅ Specific 4-card hands (AsKdQhJc)
- ✅ Omaha percentage ranges
- ✅ Complex Omaha combinations
- ✅ Game type detection

### Running Tests

```bash
cd build
make test_advanced_range_parser
make test_omaha_range_parser

./tests/test_advanced_range_parser    # Should show 10/10 passing
./tests/test_omaha_range_parser       # Should show 36/38 passing
```

---

## Future Enhancements

### Planned Features

1. **Stud Range Parser**: Full implementation of stud syntax with hole cards and upcards
2. **Weighted Ranges**: Support for hand weighting (e.g., "AA@100, KK@50")
3. **Cached Rankings**: Pre-computed hand rankings for faster percentage generation
4. **Range Visualization**: Export ranges to visual formats
5. **Range Algebra**: More complex set operations
6. **Multi-way Ranges**: Support for defining ranges for 3+ players simultaneously

### Known Limitations

1. **Mixed Comma Patterns**: "AAxx, JT98r" may fail in some contexts (under investigation)
2. **Stud Syntax**: Stud-specific patterns not yet fully implemented
3. **Maximum Range Size**: Limited to `ARP_MAX_RANGE_SIZE` (2048 hands)
4. **Percentage Rankings**: Currently only available for Hold'em and Omaha

---

## References

- **Source Code**: `src/equity/AdvancedRangeParser.c`
- **Header**: `include/poker_eval/equity/AdvancedRangeParser.h`
- **Tests**: `tests/test_advanced_range_parser.c`, `tests/test_omaha_range_parser.c`
- **Examples**: `examples/advanced_range_example.c`

---

## Version History

- **v2.0** (2025-01-03): Major refactor with expression tree parsing, PLO support
- **v1.5** (2024): Added percentage ranges and operator support
- **v1.0** (2024): Initial implementation with basic Hold'em ranges

---

**Last Updated**: January 2025
**Maintainer**: poker-eval contributors
**License**: GPL v3
