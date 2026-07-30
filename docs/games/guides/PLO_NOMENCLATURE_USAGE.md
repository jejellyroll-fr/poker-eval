# User Guide for PLO Nomenclature Module

## Overview

The PLONomenclature module provides functionality to parse, categorize, and manipulate Pot-Limit Omaha (PLO) hands according to the standard nomenclature used by coaches and solvers.

## Core Features

### 1. PLO Hand Parsing

The module can parse two types of notation:

#### Specific Hands
```c
PLOHand hand;
PLO_ParseHand("AsKdQhJc", &hand);  // Hand with specific cards
```

#### Patterns with Placeholders
```c
PLOHand hand;
PLO_ParseHand("AAxxds", &hand);    // Ace pair double-suited
PLO_ParseHand("JT98r", &hand);     // Rainbow rundown
PLO_ParseHand("KKxxss", &hand);    // King pair single-suited
```

### 2. Automatic Categorization

Hands are automatically categorized into 21 categories:
- **Unpaired** : Double-suited (DS), Single-suited (SS), Rainbow (RB)
- **One-Pair** : Pair DS, Pair SS, Pair RB
- **Two-Pair** : 2-Pair DS, 2-Pair SS, 2-Pair RB
- **Trips** : Trips DS, Trips SS, Trips RB
- **Aces** : AA DS, AA SS, AA RB
- **Broadway-heavy** : 3+ Broadway DS, SS, RB
- **Ragged/Low** : Ragged DS, SS, RB

### 3. Pattern Matching

```c
PLOHand hand;
PLO_ParseHand("AsAdKhQd", &hand);
if (PLO_MatchesPattern(&hand, "AAxxds")) {
    // The hand matches the AA double-suited pattern
}
```

## Complete Usage Example

```c
#include <poker_eval/distributions/plo_nomenclature.h>
#include <stdio.h>

int main() {
    PLOHand hand;
    
    // Parse a hand
    if (PLO_ParseHand("AAKQds", &hand)) {
        printf("Parsed hand: %s\n", hand.notation);
        printf("Category: %s\n", PLO_CategoryName(hand.category));
        printf("Suitedness: %s\n", PLO_SuitednessSuffix(hand.suitedness));
        printf("Contains an Ace: %s\n", hand.has_ace ? "Yes" : "No");
        printf("Broadway cards: %d\n", hand.broadway_count);
        
        // Check if it matches a pattern
        if (PLO_MatchesPattern(&hand, "AAxxds")) {
            printf("This hand is AA double-suited\n");
        }
        
        // Get category percentage
        float pct = PLO_CategoryPercentage(hand.category);
        printf("This category accounts for %.2f%% of hands\n", pct);
    }
    
    return 0;
}
```

## Integration with Advanced Range Parser

Categories can be inserted directly into the `AdvancedRangeParser` using the `cat:` or `category:` prefix (case, hyphens, and underscores are ignored). This avoids having to write out long PLO hand expressions manually.

```c
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

arp_range_t omaha_range;
if (ARP_ParseRange("cat:aa_ds + cat:unpaired_ss", dead, game_omaha, &omaha_range)) {
    printf("Number of combos: %zu\n", omaha_range.count);
    ARP_FreeRange(&omaha_range);
}
```

Practical examples:

- `cat:aa_ds{50%}` weights AA double-suited hands at 50%
- `category:broadway-rb - cat:pair_rb` removes rainbow hands containing a pair
- `cat:unpaired_ds, !cat:ragged_ds` excludes low double-suited hands

Aliases (`AA-DS`, `aces_ds`, `UNPAIRED-SS`, etc.) are accepted and dead cards are automatically filtered out during expansion.

## Data Structures

### PLOHand
```c
typedef struct {
    StdDeck_CardMask cards;       // The 4 cards
    PLOHandCategory category;     // Category (1-21)
    PLOSuitedness suitedness;     // Suitedness type
    PLOConnectivity connectivity; // Connectivity type
    int has_ace;                  // Contains at least one Ace
    int broadway_count;           // Number of Broadway cards (T-A)
    int pair_count;               // Number of pairs (0-2)
    int trips;                    // Has trips (1) or not (0)
    char notation[32];            // Notation string
} PLOHand;
```

### Suitedness Types
- `PLO_SUIT_RAINBOW` : 4 different suits
- `PLO_SUIT_SINGLE` : A single suit represented twice
- `PLO_SUIT_DOUBLE` : Two suits represented twice each
- `PLO_SUIT_TRIPLE` : A single suit represented 3 times
- `PLO_SUIT_QUAD` : All cards of the same suit (monotone)

### Connectivity Types
- `PLO_CONN_NONE` : No connectivity
- `PLO_CONN_RUNDOWN` : 4 consecutive cards (e.g., JT98)
- `PLO_CONN_1GAP` : One gap in sequence (e.g., JT86)
- `PLO_CONN_2GAP` : Two gaps in sequence
- `PLO_CONN_PARTIAL` : Partial connectivity

## Compilation

The module is automatically included in the poker_eval library. To compile a program using it:

```bash
gcc -o my_program my_program.c -lpoker_eval -lpoker_distributions
```

## Important Notes

1. Placeholder 'x' characters in patterns represent any card other than the specified fixed cards.
2. The `PLO_ParseHand` function generates a valid card mask for patterns with placeholders.
3. Category percentages are approximate and based on standard PLO nomenclature.
