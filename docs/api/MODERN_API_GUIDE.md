# Modern API Guide - poker-eval

This guide provides a comprehensive overview of the modern `pe_*` API for poker hand evaluation, range parsing, and equity calculations.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Range API](#range-api)
3. [Equity API](#equity-api)
4. [Solver v3 C façade](#solver-v3-c-façade)
5. [Game Variants](#game-variants)
6. [Error Handling](#error-handling)
7. [Examples by Game Type](#examples-by-game-type)
8. [Performance Tips](#performance-tips)

---

## Quick Start

### Headers

```c
#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <poker_eval/deck/deck_std.h>
```

### Basic Example: Preflop Equity

```c
#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <stdio.h>

int main(void) {
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);
    
    // Parse ranges
    pe_range_t *r1 = NULL, *r2 = NULL;
    pe_range_parse(game_holdem, "AA,KK", dead, NULL, &r1);
    pe_range_parse(game_holdem, "QQ,JJ,TT", dead, NULL, &r2);
    
    // Calculate equity
    const pe_range_t *ranges[] = {r1, r2};
    pe_equity_result_multi_t result;
    pe_equity_preflop(game_holdem, ranges, 2, NULL, &result);
    
    printf("Player 1: %.1f%%\n", result.results[0].equity * 100);
    printf("Player 2: %.1f%%\n", result.results[1].equity * 100);
    
    // Cleanup
    pe_range_free(r1);
    pe_range_free(r2);
    return 0;
}
```

## Solver v3 C façade

The stable C binding exposes the executable v3 lifecycle through
`pe_solver_api_*`. The façade borrows a caller-owned `pe_vector_game_t`, so the
game callbacks and their state must remain alive until
`pe_solver_api_free()`. The legacy `pe_cfr_*` functions are kept unchanged.

```c
#include <poker_eval_api.h>

pe_solver_config_t config = pe_solver_config_default();
pe_vector_game_t game = {0};
/* Fill game.root, game.user and the vector-game callbacks here. */
game.player_count = 2;
game.combo_count = 1;
config.algorithm.traversal = PE_TRAVERSAL_FULL_VECTOR;
config.max_iterations = 1000;
config.problem.expected_infosets = 100;
config.problem.expected_actions = 2;
config.problem.expected_combos = 1;

pe_solver_api_handle_t solver = pe_solver_api_create(&config, &game);
if (solver != NULL && pe_solver_api_run(solver) == PE_SOLVER_OK) {
    pe_progress_t progress;
    pe_solver_api_progress(solver, &progress);
}
pe_solver_api_free(solver);
```

For target-based stopping, set `max_iterations = 0`, configure a positive
`target_exploitability_mbb`, and choose a positive `exploitability_interval`.
The target is currently executed by the full-vector traversal.

---

## Range API

### Range Syntax

#### Hold'em Notation

| Syntax | Description | Example |
|--------|-------------|---------|
| `AA` | Pocket pairs | All 6 combos of aces |
| `AKs` | Suited hands | 4 combos (AhKh, AdKd, AcKc, AsKs) |
| `AKo` | Offsuit hands | 12 combos |
| `AK` | Both suited and offsuit | 16 combos |
| `TT+` | Pairs TT and better | TT, JJ, QQ, KK, AA |
| `ATs+` | Suited connectors up | ATs, AJs, AQs, AKs |
| `22-99` | Pair range | All pairs 22 through 99 |
| `AA:0.5` | Weighted | 50% frequency |

#### Omaha Notation

| Syntax | Description |
|--------|-------------|
| `AAxx` | Any hand with AA |
| `AKds` | AK double-suited |
| `AADS` | Double-suited aces category |
| `RUNDOWN` | Connected/rundown hands |

### Parsing Ranges

```c
pe_range_t *range = NULL;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

// Basic parsing
pe_status_t st = pe_range_parse(game_holdem, "AA,KK,QQ,AKs", dead, NULL, &range);
if (st != PE_STATUS_OK) {
    fprintf(stderr, "Parse error: %s\n", pe_error_string(st));
    return 1;
}

printf("Range contains %zu combinations\n", range->count);
pe_range_free(range);
```

### Parsing Options

```c
pe_parse_opts_t opts;
pe_range_opts_init(&opts);
opts.strict_syntax = 1;      // Fail on minor errors
opts.allow_weights = 1;      // Allow "AA:0.5" notation
opts.default_weight = 1.0;   // Default weight for hands

pe_range_parse(game_holdem, "AA:0.5,KK", dead, &opts, &range);
```

### Range Operations

```c
pe_range_t *r1, *r2, *combined;

// Parse two ranges
pe_range_parse(game_holdem, "AA,KK,QQ", dead, NULL, &r1);
pe_range_parse(game_holdem, "QQ,JJ,TT", dead, NULL, &r2);

// Union (combine)
pe_range_combine(r1, r2, PE_OP_UNION, &combined);
// Result: AA,KK,QQ,JJ,TT

// Intersection (common hands)
pe_range_combine(r1, r2, PE_OP_INTERSECT, &combined);
// Result: QQ only

// Difference (remove)
pe_range_combine(r1, r2, PE_OP_DIFFERENCE, &combined);
// Result: AA,KK (r1 minus QQ)

pe_range_free(r1);
pe_range_free(r2);
pe_range_free(combined);
```

### Top Percent Ranges

```c
pe_range_t *top10 = NULL;
pe_range_top_percent(game_holdem, 10.0, dead, &top10);
// Contains approximately top 10% of starting hands
```

### Filtering Dead Cards

```c
// Mark board cards as dead
StdDeck_CardMask board;
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

pe_range_t *filtered = NULL;
pe_range_filter_dead(range, board, &filtered);
```

---

## Equity API

### Result Structures

```c
// Single player result
typedef struct {
    double equity;    // Total equity (0.0 to 1.0)
    double win_prob;  // Win probability
    double tie_prob;  // Tie probability
    double ev;        // Expected value
} pe_equity_result_t;

// Multiway result
typedef struct {
    int num_players;
    pe_equity_result_t results[10];
    pe_hilo_equity_result_t hilo_results[10];
    long samples;     // Number of samples
    int exact;        // 1 if exact, 0 if Monte Carlo
} pe_equity_result_multi_t;
```

### Calculation Options

```c
pe_equity_opts_t opts = {
    .is_monte_carlo = 0,    // Auto-select method
    .iterations = 200000,   // MC iterations
    .timeout_ms = 0         // No timeout
};
```

### Preflop Equity

Always uses Monte Carlo sampling:

```c
const pe_range_t *ranges[] = {r1, r2};
pe_equity_result_multi_t result;
pe_equity_opts_t opts = {.iterations = 50000};

pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);

printf("P1: %.2f%% (win: %.2f%%, tie: %.2f%%)\n",
       result.results[0].equity * 100,
       result.results[0].win_prob * 100,
       result.results[0].tie_prob * 100);
```

### Postflop Equity

Uses exact enumeration for river, Monte Carlo for earlier streets:

```c
EvalConfig cfg = eval_config_holdem();
EvalContext *ctx = eval_context_create(&cfg);

StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_range_t *r1 = NULL, *r2 = NULL;
pe_range_parse(game_holdem, "AA,KK", dead, NULL, &r1);
pe_range_parse(game_holdem, "QQ,JJ", dead, NULL, &r2);
const pe_range_t *ranges[] = {r1, r2};

// Set up flop board
StdDeck_CardMask board;
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_HEARTS));

pe_equity_result_multi_t result;
pe_equity_multiway(ctx, game_holdem, ranges, 2, board, dead, NULL, &result);

pe_range_free(r1);
pe_range_free(r2);
eval_context_destroy(ctx);
```

### Range vs Range (Heads-up)

```c
pe_equity_range_vs_range(ctx, game_holdem, r1, r2, board, dead, NULL, &result);
```

---

## Game Variants

### Supported Games

| Enum | Game Type | Hole Cards |
|------|-----------|------------|
| `game_holdem` | Texas Hold'em | 2 |
| `game_holdem8` | Hold'em Hi/Lo | 2 |
| `game_omaha` | Omaha | 4 |
| `game_omaha8` | Omaha Hi/Lo | 4 |
| `game_omaha5` | 5-Card Omaha | 5 |
| `game_omaha6` | 6-Card Omaha | 6 |
| `game_7stud` | 7-Card Stud | 7 |
| `game_7stud8` | Stud Hi/Lo | 7 |
| `game_razz` | Razz | 7 |
| `game_5draw` | 5-Card Draw | 5 |
| `game_lowball27` | 2-7 Lowball | 5 |

### Evaluation Contexts

```c
// Hold'em context
EvalConfig cfg = eval_config_holdem();
EvalContext *ctx = eval_context_create(&cfg);

// Omaha context
EvalConfig omaha_cfg = eval_config_omaha();
EvalContext *omaha_ctx = eval_context_create(&omaha_cfg);

// Short deck (6+) context
EvalConfig short_cfg = eval_config_shortdeck();
EvalContext *short_ctx = eval_context_create(&short_cfg);
```

---

## Error Handling

### Status Codes

```c
typedef enum {
    PE_STATUS_OK = 0,             // Success
    PE_STATUS_ERROR = 1,          // Generic error
    PE_STATUS_INVALID_ARG = 2,    // Invalid argument
    PE_STATUS_PARSE_ERROR = 3,    // Range parsing failed
    PE_STATUS_OUT_OF_MEMORY = 4,  // Allocation failed
    PE_STATUS_NOT_IMPLEMENTED = 5 // Not implemented
} pe_status_t;
```

### Error Messages

```c
pe_status_t st = pe_range_parse(game_holdem, "INVALID", dead, NULL, &range);
if (st != PE_STATUS_OK) {
    fprintf(stderr, "Error: %s\n", pe_error_string(st));
}
```

### Robust Error Handling Pattern

```c
const char *range1_str = "AA,KK";
const char *range2_str = "QQ,JJ";
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_range_t *r1 = NULL, *r2 = NULL;
pe_equity_result_multi_t result;
int ret = 0;

if (pe_range_parse(game_holdem, range1_str, dead, NULL, &r1) != PE_STATUS_OK) {
    fprintf(stderr, "Failed to parse range 1\n");
    ret = 1;
    goto cleanup;
}

if (pe_range_parse(game_holdem, range2_str, dead, NULL, &r2) != PE_STATUS_OK) {
    fprintf(stderr, "Failed to parse range 2\n");
    ret = 1;
    goto cleanup;
}

const pe_range_t *ranges[] = {r1, r2};
if (pe_equity_preflop(game_holdem, ranges, 2, NULL, &result) != PE_STATUS_OK) {
    fprintf(stderr, "Equity calculation failed\n");
    ret = 1;
    goto cleanup;
}

// Use result...

cleanup:
    pe_range_free(r1);
    pe_range_free(r2);
    return ret;
```

---

## Examples by Game Type

### Texas Hold'em

```c
pe_range_t *hero = NULL, *villain = NULL;
StdDeck_CardMask dead, board;
StdDeck_CardMask_RESET(dead);
StdDeck_CardMask_RESET(board);

// Preflop: AA vs random
pe_range_parse(game_holdem, "AA", dead, NULL, &hero);
pe_range_parse(game_holdem, "22+,A2s+,K9s+,Q9s+,J9s+,T9s,A9o+,KTo+,QTo+,JTo", 
               dead, NULL, &villain);

const pe_range_t *ranges[] = {hero, villain};
pe_equity_result_multi_t result;
pe_equity_preflop(game_holdem, ranges, 2, NULL, &result);

printf("AA vs ~30%% range: %.1f%%\n", result.results[0].equity * 100);

pe_range_free(hero);
pe_range_free(villain);
```

### Omaha

```c
pe_range_t *r1 = NULL, *r2 = NULL;
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_range_parse(game_omaha, "AAxx", dead, NULL, &r1);
pe_range_parse(game_omaha, "KKxx", dead, NULL, &r2);

const pe_range_t *ranges[] = {r1, r2};
pe_equity_result_multi_t result;
pe_equity_opts_t opts = {.iterations = 100000};
pe_equity_preflop(game_omaha, ranges, 2, &opts, &result);

printf("AAxx vs KKxx: %.1f%% vs %.1f%%\n",
       result.results[0].equity * 100,
       result.results[1].equity * 100);

pe_range_free(r1);
pe_range_free(r2);
```

### Omaha Hi/Lo

```c
pe_range_parse(game_omaha8, "AA23ds", dead, NULL, &r1);  // Good hi/lo hand
pe_range_parse(game_omaha8, "KKQJ", dead, NULL, &r2);    // High-only hand

const pe_range_t *ranges[] = {r1, r2};
pe_equity_result_multi_t result;
pe_equity_preflop(game_omaha8, ranges, 2, NULL, &result);

// Check hi/lo results
printf("P1 Hi: %.1f%%, Lo: %.1f%%, Scoop: %.1f%%\n",
       result.hilo_results[0].equity_hi * 100,
       result.hilo_results[0].equity_lo * 100,
       result.hilo_results[0].scoop_prob * 100);
```

### Multiway Pot

```c
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_range_t *r1 = NULL, *r2 = NULL, *r3 = NULL;
pe_range_parse(game_holdem, "AA,KK", dead, NULL, &r1);
pe_range_parse(game_holdem, "QQ,JJ", dead, NULL, &r2);
pe_range_parse(game_holdem, "TT,99,88", dead, NULL, &r3);

const pe_range_t *ranges[] = {r1, r2, r3};
pe_equity_result_multi_t result;
pe_equity_preflop(game_holdem, ranges, 3, NULL, &result);

for (int i = 0; i < 3; i++) {
    printf("Player %d: %.1f%%\n", i+1, result.results[i].equity * 100);
}

pe_range_free(r1);
pe_range_free(r2);
pe_range_free(r3);
```

---

## Performance Tips

### 1. Reuse Contexts

```c
EvalConfig cfg = eval_config_holdem();
EvalContext *ctx = eval_context_create(&cfg);

StdDeck_CardMask dead, board;
StdDeck_CardMask_RESET(dead);
StdDeck_CardMask_RESET(board);

pe_range_t *r1 = NULL, *r2 = NULL;
pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
const pe_range_t *ranges[] = {r1, r2};
pe_equity_result_multi_t result;
int num_calculations = 10;

// Use multiple times
for (int i = 0; i < num_calculations; i++) {
    pe_equity_multiway(ctx, game_holdem, ranges, 2, board, dead, NULL, &result);
}

// Destroy once
pe_range_free(r1);
pe_range_free(r2);
eval_context_destroy(ctx);
```

### 2. Adjust Monte Carlo Iterations

```c
// Quick estimate (faster, less accurate)
pe_equity_opts_t quick = {.iterations = 10000};

// Standard accuracy
pe_equity_opts_t standard = {.iterations = 100000};

// High accuracy (slower)
pe_equity_opts_t accurate = {.iterations = 500000};
```

### 3. Pre-filter Ranges

```c
// Filter dead cards before equity calculation
pe_range_t *filtered = NULL;
pe_range_filter_dead(range, board, &filtered);
// Use filtered range for faster calculations
```

### 4. Use Exact Enumeration When Possible

River calculations with small ranges are exact and fast:

```c
EvalConfig cfg = eval_config_holdem();
EvalContext *ctx = eval_context_create(&cfg);

StdDeck_CardMask dead, river_board;
StdDeck_CardMask_RESET(dead);
StdDeck_CardMask_RESET(river_board); // 5 cards

pe_range_t *r1 = NULL, *r2 = NULL;
pe_range_parse(game_holdem, "AA", dead, NULL, &r1);
pe_range_parse(game_holdem, "KK", dead, NULL, &r2);
const pe_range_t *ranges[] = {r1, r2};
pe_equity_result_multi_t result;

// This will use exact enumeration (5-card board)
pe_equity_multiway(ctx, game_holdem, ranges, 2, river_board, dead, NULL, &result);
// result.exact == 1

pe_range_free(r1);
pe_range_free(r2);
eval_context_destroy(ctx);
```

---

## See Also

- [PE_EQUITY_API_GUIDE.md](guides/PE_EQUITY_API_GUIDE.md) - Detailed equity API reference
- [BETTING_API_GUIDE.md](guides/BETTING_API_GUIDE.md) - Betting round management

---

*Last updated: January 2025*
