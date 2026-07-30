# API Reference: Pre-flop and Flop Equity

This document provides a detailed reference for the Pre-flop and Flop Equity modules in `poker-eval`.

## Pre-flop Equity (`poker_eval/equity/preflop_equity.h`)

The Pre-flop Equity module provides tools for calculating equity between hand ranges before the flop, analyzing hand strength, and managing hand ranges.

### Constants

*   `PREFLOP_NUM_CANONICAL_HANDS`: 169 (13 pairs + 78 suited + 78 offsuit).
*   `PREFLOP_LOOKUP_TABLE_SIZE`: 28,561 (169 * 169).

### Types

#### `preflop_hand_t`
```c
typedef int preflop_hand_t;  /* 0-168: canonical hand index */
```
Represents a canonical Hold'em starting hand. Indices map to:
*   0-12: Pairs (AA-22)
*   13-90: Suited hands (AKs-32s)
*   91-168: Offsuit hands (AKo-32o)

#### `preflop_strength_t`
```c
typedef enum {
    PREFLOP_STRENGTH_PREMIUM,   /* AA, KK, QQ, AKs */
    PREFLOP_STRENGTH_STRONG,    /* JJ, TT, AQs, AKo */
    PREFLOP_STRENGTH_MEDIUM,    /* 99-66, AJs, KQs */
    PREFLOP_STRENGTH_WEAK,      /* Small pairs, suited connectors */
    PREFLOP_STRENGTH_TRASH      /* Junk */
} preflop_strength_t;
```
Categorizes hand strength for strategy logic.

#### `preflop_range_t`
```c
typedef struct {
    uint32_t bitmap[(PREFLOP_NUM_CANONICAL_HANDS + 31) / 32];
    int num_hands;
} preflop_range_t;
```
A bitset representing a range of hands.

#### `preflop_equity_input_t`
```c
typedef struct {
    preflop_range_t range1;
    preflop_range_t range2;
    size_t num_samples; /* 0 = exhaustive enumeration, >0 = Monte Carlo */
    preflop_lookup_table_t *lookup_table; /* Optional: use for instant lookups */
} preflop_equity_input_t;
```
Input parameters for equity calculation.

#### `preflop_equity_result_t`
```c
typedef struct {
    double equity1;
    double equity2;
    double tie_equity;
    size_t num_matchups;
    double **equity_matrix; /* [169][169] detailed breakdown, NULL if not requested */
} preflop_equity_result_t;
```
Results of the equity calculation.

### Functions

#### Hand Conversion

*   `preflop_hand_t preflop_cards_to_hand(int card1, int card2)`
    *   Converts two cards (StdDeck format) to a canonical hand index.
*   `void preflop_hand_to_string(preflop_hand_t hand, char *out_str)`
    *   Converts a hand index to string (e.g., "AKs").
*   `int preflop_string_to_hand(const char *str)`
    *   Parses a hand string (e.g., "AKs", "QQ") to an index.
*   `preflop_strength_t preflop_classify(preflop_hand_t hand)`
    *   Returns the strength category of a hand.

#### Range Operations

*   `void preflop_range_init(preflop_range_t *range)`
    *   Initializes an empty range.
*   `void preflop_range_add(preflop_range_t *range, preflop_hand_t hand)`
    *   Adds a specific hand to the range.
*   `int preflop_range_contains(const preflop_range_t *range, preflop_hand_t hand)`
    *   Checks if a hand is in the range.
*   `int preflop_range_parse(const char *range_str, preflop_range_t *out_range)`
    *   Parses a range string (e.g., "QQ+, AKs"). Supports standard notation including plus (`+`).
*   `void preflop_range_to_string(const preflop_range_t *range, char *out_str, size_t max_len)`
    *   Serializes a range to a string.

#### Equity Calculation

*   `int preflop_equity_calculate(const preflop_equity_input_t *input, preflop_equity_result_t *result)`
    *   Calculates equity between two ranges.
    *   Supports exhaustive enumeration (slow, exact) and Monte Carlo (fast, approximate).
    *   Can use a lookup table for instant 1v1 equity.

## Flop Equity (`poker_eval/equity/flop_equity.h`)

The Flop Equity module handles post-flop analysis, including texture analysis and equity calculation.

### Types

#### `flop_texture_category_t`
```c
typedef enum {
    FLOP_TEXTURE_DRY,
    FLOP_TEXTURE_WET,
    FLOP_TEXTURE_COORDINATED,
    FLOP_TEXTURE_PAIRED,
    FLOP_TEXTURE_TRIPS
} flop_texture_category_t;
```
High-level classification of flop texture.

#### `flop_analysis_t`
```c
typedef struct {
    /* Board properties */
    bool is_paired, is_trips;
    bool is_monotone, is_two_tone, is_rainbow;

    /* Rank properties */
    int high_card_rank, middle_card_rank, low_card_rank;
    int paired_rank;

    /* Connectivity */
    bool is_connected;
    int max_gap;
    int n_broadway, n_low_cards;

    /* Draw potential */
    int straight_draw_outs, flush_draw_outs;
    bool has_oesd, has_gutshot;

    /* Overall */
    flop_texture_category_t texture;
    int texture_score; /* 0 (Dry) - 100 (Wet) */
} flop_analysis_t;
```
Detailed analysis of the flop board.

#### `flop_equity_input_t`
```c
typedef struct {
    StdDeck_CardMask pocket;
    StdDeck_CardMask flop;
    int n_opponents;
    int n_samples; /* 0 = exhaustive */
} flop_equity_input_t;
```

#### `flop_equity_result_t`
```c
typedef struct {
    double equity;
    double variance;

    /* Probabilities of made hands */
    double prob_high_card, prob_pair, prob_two_pair;
    double prob_trips, prob_straight, prob_flush;
    double prob_full_house, prob_quads;

    /* Probabilities of draws */
    double prob_flush_draw, prob_oesd, prob_gutshot;
    double prob_backdoor_flush, prob_backdoor_straight;

    /* Improvement odds */
    double turn_improvement;
    double river_improvement;
    double runner_runner_improvement;

    /* Outs counting */
    int made_hand_outs;
    int draw_outs;
} flop_equity_result_t;
```

### Functions

#### Texture Analysis

*   `int analyze_flop_texture(StdDeck_CardMask flop, flop_analysis_t *analysis)`
    *   Analyzes the flop and populates the `flop_analysis_t` structure.
*   `void flop_texture_to_string(flop_texture_category_t texture, char *out, size_t out_size)`
    *   Returns a string representation of the texture category.

#### Equity Calculation

*   `int flop_calc_equity(const flop_equity_input_t *input, flop_equity_result_t *result)`
    *   Calculates the equity of a hand against random opponents on a given flop.
    *   Provides detailed breakdown of made hands, draws, and improvement probabilities.

#### Utility

*   `int flop_count_outs(StdDeck_CardMask pocket, StdDeck_CardMask flop, int *flush_outs, int *straight_outs, int *pair_outs)`
    *   Counts the number of immediate outs to improve the hand.
