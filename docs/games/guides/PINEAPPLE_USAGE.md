# Pineapple Hold'em Usage Guide

## Overview

Pineapple Hold'em is a variant of Texas Hold'em where each player receives 3 hole cards instead of 2. After the flop is dealt, each player must discard one of their hole cards, leaving them with 2 cards for the remainder of the hand.

This implementation automatically simulates the optimal discard strategy by evaluating all possible 2-card combinations from the 3 hole cards and choosing the best one.

## Game Rules

1. Each player receives 3 hole cards
2. Pre-flop betting round
3. Flop (3 community cards) is dealt
4. Each player discards 1 hole card (keeping the best 2)
5. Turn and river betting rounds proceed as in regular Hold'em
6. Showdown uses the remaining 2 hole cards + 5 community cards

## API Usage

### Basic Evaluation

```c
#include "poker_eval/poker_defs.h"
#include "rules_pineapple.h"

StdDeck_CardMask pocket, board;
HandVal hand_value;

// Set up 3 hole cards (e.g., As Ah Kh)
StdDeck_CardMask_RESET(pocket);
StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
StdDeck_CardMask_SET(pocket, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

// Set up board (e.g., 2c 3d 4s 5h 6c)
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
// ... set other board cards

// Evaluate the best possible hand
hand_value = Pineapple_EVAL(pocket, board);
```

### Finding Optimal Discard

```c
#include "rules_pineapple.h"

StdDeck_CardMask best_two_cards = Pineapple_FindBestDiscard(pocket, board);
// Returns the best 2-card combination from the 3 hole cards
```

### Equity Calculation

```c
#include "poker_eval/enumerate.h"
#include "poker_eval/enumdefs.h"

StdDeck_CardMask pockets[2];
StdDeck_CardMask board, dead;
enum_result_t result;

// Set up hands and run simulation
int err = enumSample(game_pineapple, pockets, board, dead, 2, 0, 10000, 0, &result);

// Get equity percentages
double player1_equity = 100.0 * result.ev[0] / result.nsamples;
double player2_equity = 100.0 * result.ev[1] / result.nsamples;
```

## Command Line Usage (pokenum)

The `pokenum` tool supports Pineapple Hold'em with the `-pa` flag:

```bash
# Basic equity calculation (pre-flop)
./pokenum -pa AsAhKh - KsKdQh

# With a flop
./pokenum -pa AsAhKh - KsKdQh -- 2c7dJh

# Monte Carlo simulation
./pokenum -pa -mc 10000 AsAhKh - KsKdQh

# Multiple players
./pokenum -pa AsAhKh - KsKdQh - QcQsJd
```

## Examples

### Example 1: Pocket Aces vs Pocket Kings

```c
// Player 1: As Ah Kh (will keep As Ah, discard Kh)
// Player 2: Ks Kd Qh (will keep Ks Kd, discard Qh)
// Expected: Player 1 should have ~80% equity
```

### Example 2: Straight Draw Scenario

```c
// Player 1: 7h 8s 9c
// Board: Tc Jd 2h 3s 4c
// Optimal play: Keep 8s 9c to make straight (7-8-9-T-J)
```

### Example 3: Three of a Kind

```c
// Player 1: Qs Qh Qc
// Board: 2c 3d 4s 5h 6c
// Optimal play: Keep any two queens (all combinations are equivalent)
```

## Implementation Details

### Evaluation Algorithm

The Pineapple evaluation works by:

1. Extracting all 3 hole cards from the pocket mask
2. Generating all 3 possible 2-card combinations (C(3,2) = 3)
3. For each combination:
   - Combine the 2 cards with the 5-card board
   - Evaluate the resulting 7-card hand using standard Hold'em rules
4. Return the best hand value among all combinations

### Performance Considerations

- Each Pineapple evaluation requires 3 standard Hold'em evaluations
- Monte Carlo simulations scale linearly with the number of iterations
- For large-scale simulations, consider using the batched evaluation functions

### Limitations

- Currently only supports high-only games (no hi/lo split)
- Assumes optimal discard strategy (players always make the mathematically best discard)
- Does not model psychological aspects of discard decisions

## Integration with Existing Code

Pineapple Hold'em integrates seamlessly with the existing poker-eval framework:

- Uses the same `StdDeck_CardMask` representation
- Compatible with all enumeration functions (`enumSample`, `enumExhaustive`)
- Follows the same API patterns as other game variants
- Can be used in range vs range equity calculations

## Testing

The implementation includes comprehensive tests:

- Basic evaluation correctness
- Optimal discard selection
- Equity calculation accuracy
- Edge cases (insufficient cards, etc.)

Run tests with:
```bash
./test_pineapple_final
```

## Future Enhancements

Potential improvements for future versions:

1. **Pineapple Hi/Lo**: Split pot variant with low qualifiers
2. **Crazy Pineapple**: Discard after the turn instead of flop
3. **Lazy Pineapple**: Optional discard (can keep all 3 cards)
4. **Performance Optimizations**: SIMD acceleration for batch evaluations
5. **Range Analysis**: Specialized tools for Pineapple range vs range calculations

## References

- [Pineapple Hold'em Rules](https://en.wikipedia.org/wiki/Pineapple_poker)
- [Poker Hand Evaluation](https://en.wikipedia.org/wiki/Poker_hand)
- [Monte Carlo Methods in Poker](https://en.wikipedia.org/wiki/Monte_Carlo_method)
