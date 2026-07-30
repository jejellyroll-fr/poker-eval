# Core Module

This module contains the fundamental functionality for poker evaluation and card, mask, and combination manipulation.

## Source Files (`src/core/`)

### Evaluation and Hand Value
- `CardConverter.c` - Conversion between card representations and strings
- `canonical_5card.c` - Canonization and equivalence of 5-card hands
- `card.c` - Base representation of cards and card masks
- `combo_7to5.c` - Enumeration of 7-card to 5-card combinations
- `combo_7to5_hilo.c` - Enumeration of 7-card Hi-Lo (high/low) combinations
- `eval_7c_simd.c` - SIMD-optimized 7-card evaluation
- `eval_cache.c` - Hand evaluation cache
- `eval_context.c` - Evaluation context and state
- `evx.c` - EVX algorithm and evaluation
- `low_eval.c` - Low hand evaluation (Lowball, Razz, 8-or-better)
- `low_qualifier.c` - Low hand qualification (e.g., 8-or-better)
- `modern_cardmask.c` - Modern card masks and indexing bitmasks

### Deck Management
- `deck.c` - Generic card deck interface
- `deck_std.c` - Standard deck (52 cards)
- `deck_joker.c` - Joker deck (53 cards)
- `deck_short.c` - Short Deck / Six-Plus Hold'em (36 cards)
- `deck_astud.c` - Asian Stud deck (32 cards)
- `universal_deck.c` - Universal deck abstraction for different game formats
- `joker_expansion_controlled.c` - Controlled Joker card management and expansion

### Combinations and Status
- `modern_combinations.c` - Modern combination calculations
- `omaha_combinations.c` - Omaha-specific combinations and subsets
- `status.c` - Core module status codes, diagnostics, and errors
- `deterministic_benchmark.c` - Harness for core deterministic benchmarks

### Lookup Tables and Generators
- `t_astudcardmasks.c` - Mask table/generator for Asian Stud
- `t_botcard.c` - Lowest card table/generator
- `t_botfivecards.c` - Lowest 5 cards table/generator
- `t_cardmasks.c` - Standard card mask table/generator
- `t_jokercardmasks.c` - Joker card mask table/generator
- `t_nbits.c` - Set bit count table and calculations (popcount)
- `t_shortdeckcardmasks.c` - Mask table/generator for Short Deck
- `t_straight.c` - Straight detection table/generator
- `t_topcard.c` - Highest card table/generator
- `t_topfivecards.c` - Highest 5 cards table/generator