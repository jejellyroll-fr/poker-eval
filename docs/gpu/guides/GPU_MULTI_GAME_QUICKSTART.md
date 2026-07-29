# GPU Multi-Game Evaluation - Quick Start Guide

**Date**: 2025-11-07
**Audience**: Developers using the poker evaluation library

---

## Overview

The GPU evaluation now supports **multiple poker variants** beyond just Hold'em:
- Hold'em & Hold'em Hi/Lo
- Omaha (4, 5, 6 cards) & Omaha Hi/Lo
- 7-Card Stud & Stud Hi/Lo
- Razz (A-5 Lowball)

---

## Basic Usage

### 1. Create a Game Configuration

```c
#include <poker_eval/gpu/eval_gpu.h>

/* Hold'em */
gpu_game_config_t holdem_config = gpu_game_config_holdem();

/* Omaha-4 */
gpu_game_config_t omaha_config = gpu_game_config_omaha(4);

/* Omaha Hi/Lo */
gpu_game_config_t omaha8_config = gpu_game_config_omaha8(4);

/* Razz */
gpu_game_config_t razz_config = gpu_game_config_razz();

/* 7-Card Stud */
gpu_game_config_t stud_config = gpu_game_config_stud();
```

### 2. Initialize GPU Context

```c
/* Old API (still works, defaults to Hold'em) */
gpu_eval_context_t* ctx = gpu_eval_init(0, 1000, 0);

/* New API (specify game) */
gpu_eval_context_t* ctx = gpu_eval_init_game(
    0,              // device_id
    1000,           // max_batch_size
    0,              // backend_type (0=CUDA, 1=OpenCL)
    omaha_config    // game configuration
);
```

### 3. Evaluate Hands

#### For High-only games (Hold'em, Omaha, Stud)

```c
/* Prepare data */
StdDeck_CardMask boards[num_boards];
StdDeck_CardMask hole_cards[num_boards * num_players];
/* ... fill with card data ... */

/* Evaluate using old API (still works) */
gpu_eval_result_t result = {0};
int ret = gpu_eval_batch_boards(ctx, boards, hole_cards,
                                 num_boards, num_players, &result);

if (ret == 0) {
    for (int i = 0; i < num_boards * num_players; i++) {
        printf("Hand %d value: %u\n", i, result.hand_values[i]);
    }
}
```

#### For Hi/Lo games (Omaha8, Holdem8, Stud8)

```c
/* Use new hi/lo API */
gpu_eval_result_hilo_t result = {0};
int ret = gpu_eval_batch_boards_hilo(ctx, boards, hole_cards,
                                      num_boards, num_players, &result);

if (ret == 0) {
    for (int i = 0; i < num_boards * num_players; i++) {
        printf("Hand %d:\n", i);
        printf("  Hi value: %u\n", result.hand_values_hi[i]);
        if (result.lo_qualifies[i]) {
            printf("  Lo value: %u (qualifies)\n", result.hand_values_lo[i]);
        } else {
            printf("  No qualifying low\n");
        }
    }
}
```

### 4. Cleanup

```c
gpu_eval_cleanup(ctx);
```

---

## Complete Examples

### Example 1: Omaha High Evaluation

```c
#include <poker_eval/gpu/eval_gpu.h>
#include <poker_eval/deck/deck_std.h>

int main() {
    /* Create Omaha configuration */
    gpu_game_config_t config = gpu_game_config_omaha(4);

    /* Initialize GPU */
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, 100, 0, config);
    if (!ctx) {
        fprintf(stderr, "GPU initialization failed\n");
        return 1;
    }

    /* Create test scenario */
    StdDeck_CardMask board, holes[2];

    /* Board: Ah Kh Qh 2c 3d */
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));
    /* ... set other cards ... */

    /* Player 1: Jh Th 8c 7d (has flush draw) */
    /* Player 2: As Kc Qd 2h (has trips) */
    /* ... set hole cards ... */

    /* Evaluate */
    StdDeck_CardMask boards[1] = {board};
    StdDeck_CardMask hole_array[2] = {holes[0], holes[1]};

    gpu_eval_result_t result = {0};
    int ret = gpu_eval_batch_boards(ctx, boards, hole_array, 1, 2, &result);

    if (ret == 0) {
        printf("Player 1 hand value: %u\n", result.hand_values[0]);
        printf("Player 2 hand value: %u\n", result.hand_values[1]);

        if (result.hand_values[0] > result.hand_values[1]) {
            printf("Player 1 wins!\n");
        } else if (result.hand_values[1] > result.hand_values[0]) {
            printf("Player 2 wins!\n");
        } else {
            printf("Tie!\n");
        }
    }

    gpu_eval_cleanup(ctx);
    return 0;
}
```

### Example 2: Omaha Hi/Lo (Omaha8)

```c
int main() {
    /* Create Omaha8 configuration */
    gpu_game_config_t config = gpu_game_config_omaha8(4);

    /* Initialize GPU */
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, 100, 0, config);
    if (!ctx) return 1;

    /* Setup scenario (same as above) */
    StdDeck_CardMask board, holes[2];
    /* ... fill with cards ... */

    /* Evaluate with hi/lo support */
    StdDeck_CardMask boards[1] = {board};
    StdDeck_CardMask hole_array[2] = {holes[0], holes[1]};

    gpu_eval_result_hilo_t result = {0};
    int ret = gpu_eval_batch_boards_hilo(ctx, boards, hole_array, 1, 2, &result);

    if (ret == 0) {
        printf("=== Player 1 ===\n");
        printf("Hi: %u\n", result.hand_values_hi[0]);
        if (result.lo_qualifies[0]) {
            printf("Lo: %u (qualifies for 8-or-better)\n", result.hand_values_lo[0]);
        }

        printf("\n=== Player 2 ===\n");
        printf("Hi: %u\n", result.hand_values_hi[1]);
        if (result.lo_qualifies[1]) {
            printf("Lo: %u (qualifies)\n", result.hand_values_lo[1]);
        }

        /* Determine winner for each half of pot */
        printf("\n=== Pot Split ===\n");

        /* Hi winner */
        if (result.hand_values_hi[0] > result.hand_values_hi[1]) {
            printf("Hi: Player 1 wins\n");
        } else if (result.hand_values_hi[1] > result.hand_values_hi[0]) {
            printf("Hi: Player 2 wins\n");
        } else {
            printf("Hi: Tie\n");
        }

        /* Lo winner (lower is better) */
        if (result.lo_qualifies[0] && result.lo_qualifies[1]) {
            if (result.hand_values_lo[0] < result.hand_values_lo[1]) {
                printf("Lo: Player 1 wins\n");
            } else if (result.hand_values_lo[1] < result.hand_values_lo[0]) {
                printf("Lo: Player 2 wins\n");
            } else {
                printf("Lo: Tie\n");
            }
        } else if (result.lo_qualifies[0]) {
            printf("Lo: Player 1 wins (only qualifier)\n");
        } else if (result.lo_qualifies[1]) {
            printf("Lo: Player 2 wins (only qualifier)\n");
        } else {
            printf("Lo: No qualifier (hi wins full pot)\n");
        }
    }

    /* Cleanup */
    if (result.hand_values_hi) free(result.hand_values_hi);
    if (result.hand_values_lo) free(result.hand_values_lo);
    if (result.lo_qualifies) free(result.lo_qualifies);

    gpu_eval_cleanup(ctx);
    return 0;
}
```

### Example 3: Razz (Lowball)

```c
int main() {
    /* Create Razz configuration */
    gpu_game_config_t config = gpu_game_config_razz();

    /* Initialize GPU */
    gpu_eval_context_t* ctx = gpu_eval_init_game(0, 100, 0, config);
    if (!ctx) return 1;

    /* In Razz, all 7 cards are "hole" cards (no board) */
    StdDeck_CardMask player_hands[2];
    /* Player 1: A-2-3-4-5-8-9 (has wheel: A-2-3-4-5) */
    /* Player 2: 2-3-4-5-6-7-8 (has 8-high straight for low) */
    /* ... set cards ... */

    /* For Razz, pass empty board and all cards in hole_cards */
    StdDeck_CardMask empty_board;
    StdDeck_CardMask_RESET(empty_board);

    StdDeck_CardMask boards[1] = {empty_board};
    StdDeck_CardMask hole_array[2] = {player_hands[0], player_hands[1]};

    /* Evaluate */
    gpu_eval_result_hilo_t result = {0};
    int ret = gpu_eval_batch_boards_hilo(ctx, boards, hole_array, 1, 2, &result);

    if (ret == 0) {
        printf("Player 1 low: %u\n", result.hand_values_lo[0]);
        printf("Player 2 low: %u\n", result.hand_values_lo[1]);

        /* In Razz, LOWER value is BETTER */
        if (result.hand_values_lo[0] < result.hand_values_lo[1]) {
            printf("Player 1 wins with better low!\n");
        } else {
            printf("Player 2 wins with better low!\n");
        }
    }

    gpu_eval_cleanup(ctx);
    return 0;
}
```

---

## Configuration Reference

### Available Game Configs

| Function | Game | Hole Cards | Board Cards | Low | Split |
|----------|------|------------|-------------|-----|-------|
| `gpu_game_config_holdem()` | Hold'em | 2 | 5 | No | No |
| `gpu_game_config_holdem8()` | Hold'em Hi/Lo | 2 | 5 | Yes | Yes |
| `gpu_game_config_omaha(4)` | Omaha | 4 | 5 | No | No |
| `gpu_game_config_omaha(5)` | Omaha-5 | 5 | 5 | No | No |
| `gpu_game_config_omaha(6)` | Omaha-6 | 6 | 5 | No | No |
| `gpu_game_config_omaha8(4)` | Omaha Hi/Lo | 4 | 5 | Yes | Yes |
| `gpu_game_config_stud()` | 7-Card Stud | 7 | 0 | No | No |
| `gpu_game_config_razz()` | Razz | 7 | 0 | Yes | No |

### Game Config Structure

```c
typedef struct {
    gpu_game_type_t game;     // Game type enum
    int num_hole_cards;       // Number of hole cards per player
    int num_board_cards;      // Number of community cards
    int eval_low;             // 1 if low hands should be evaluated
    int split_pot;            // 1 if pot can be split hi/lo
    int low_qualifier;        // Qualifier (8 for 8-or-better, 0 for none)
} gpu_game_config_t;
```

---

## API Reference

### Initialization

```c
/* Legacy (Hold'em only) */
gpu_eval_context_t* gpu_eval_init(
    int device_id,
    size_t max_batch_size,
    int backend_type
);

/* New (any game) */
gpu_eval_context_t* gpu_eval_init_game(
    int device_id,
    size_t max_batch_size,
    int backend_type,
    gpu_game_config_t game_config
);
```

### Evaluation

```c
/* High-only evaluation */
int gpu_eval_batch_boards(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
);

/* Hi/Lo evaluation */
int gpu_eval_batch_boards_hilo(
    gpu_eval_context_t* ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_hilo_t* result
);
```

### Cleanup

```c
void gpu_eval_cleanup(gpu_eval_context_t* ctx);
```

---

## Common Pitfalls

### 1. Board vs Hole Cards for Stud/Razz
❌ **Wrong**: Pass cards in `boards` array for Stud
✅ **Right**: Pass empty board, all cards in `hole_cards`

```c
/* Stud/Razz: NO board */
StdDeck_CardMask empty;
StdDeck_CardMask_RESET(empty);
gpu_eval_batch_boards_hilo(ctx, &empty, player_hands, 1, num_players, &result);
```

### 2. Omaha Card Count
❌ **Wrong**: Use 2 hole cards for Omaha
✅ **Right**: Use 4, 5, or 6 hole cards

```c
/* Omaha requires 4+ hole cards */
gpu_game_config_t config = gpu_game_config_omaha(4);
```

### 3. Low Hand Interpretation
❌ **Wrong**: Higher value = better low
✅ **Right**: LOWER value = better low (inverted internally)

```c
/* Lower is better */
if (result.hand_values_lo[0] < result.hand_values_lo[1]) {
    printf("Player 0 has better low\n");
}
```

---

## Performance Tips

1. **Batch Size**: Use larger batches (500-1000) for better GPU utilization
2. **Memory**: Reuse contexts and result buffers when possible
3. **Omaha**: Omaha-6 is ~3x slower than Omaha-4 due to combinations (150 vs 60)
4. **Async**: Use CUDA streams for overlapping computation and transfers (advanced)

---

## Troubleshooting

### "GPU initialization failed"
- Check CUDA installation: `nvidia-smi`
- Verify device_id is valid
- Ensure GPU has compute capability >= 5.0

### "Undefined reference to gpu_eval_init_game"
- Rebuild library: `make clean && make`
- Check CMakeLists.txt includes new source files

### "Incorrect Omaha evaluation"
- Verify hole card count matches config
- Check that board has exactly 5 cards
- Validate input masks are not empty

---

## Next Steps

- See `GPU_SIMD_IMPLEMENTATION_STATUS.md` for implementation status
- See `GPU_SIMD_MULTI_GAME_PLAN.md` for complete architecture
- Run `tests/test_gpu_omaha` for example usage
- Check performance benchmarks (coming soon)

---

**Questions?** See project documentation or file an issue.
