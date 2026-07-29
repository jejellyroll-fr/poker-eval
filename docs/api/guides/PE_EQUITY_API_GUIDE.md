# pe_* Range and Equity API Guide

This guide covers the public `pe_*` API for ranges and equity calculations.

## Headers

```c
#include <poker_eval/range.h>
#include <poker_eval/equity.h>
#include <poker_eval/deck/deck_std.h>
```

## Range parsing and management

```c
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_range_t *r1 = NULL;
pe_range_t *r2 = NULL;

pe_status_t st = pe_range_parse(game_holdem, "AA,KK,AKs", dead, NULL, &r1);
if (st != PE_STATUS_OK) { /* handle error */ }

st = pe_range_parse(game_holdem, "JJ+,AQo+", dead, NULL, &r2);
if (st != PE_STATUS_OK) { /* handle error */ }

/* Use r1/r2 ... */

pe_range_free(r1);
pe_range_free(r2);
```

Useful helpers:
- `pe_range_combine()` to union/intersect/diff ranges.
- `pe_range_filter_dead()` to remove dead cards.
- `pe_range_top_percent()` to build top X percent ranges.

## Preflop equity

`pe_equity_preflop()` computes preflop equity for community-card games. It
forces Monte Carlo sampling and uses `opts->iterations` (default: 200000).

```c
const pe_range_t *ranges[] = { r1, r2 };
pe_equity_result_multi_t result;
pe_equity_opts_t opts = {0};
opts.is_monte_carlo = 1;
opts.iterations = 30000;

pe_status_t st = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
if (st != PE_STATUS_OK) { /* handle error */ }

printf("P1 equity=%.3f P2 equity=%.3f samples=%ld exact=%d\n",
       result.results[0].equity,
       result.results[1].equity,
       result.samples,
       result.exact);
```

## Postflop and known board equity

`pe_equity_multiway()` and `pe_equity_range_vs_range()` evaluate equity given a
known board (0-5 cards). When a full 5-card board is provided with a valid
`EvalContext`, the engine uses exact evaluation. For incomplete boards, the
engine falls back to Monte Carlo sampling via the range equity engine.

```c
EvalConfig cfg = eval_config_default();
EvalContext *ctx = eval_context_create(&cfg);

StdDeck_CardMask board;
StdDeck_CardMask_RESET(board);
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_TEN, StdDeck_Suit_SPADES));

StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

pe_equity_opts_t opts = {0};
opts.is_monte_carlo = 1;
opts.iterations = 50000;

pe_equity_result_multi_t result;
pe_status_t st = pe_equity_multiway(ctx, game_holdem, ranges, 2, board, dead, &opts, &result);
if (st != PE_STATUS_OK) { /* handle error */ }

eval_context_destroy(ctx);
```

`pe_equity_calculate()` is a legacy wrapper that forwards to
`pe_equity_multiway()` without a context.

## Options

`pe_equity_opts_t` fields:
- `is_monte_carlo`: force Monte Carlo when set to 1.
- `iterations`: number of Monte Carlo iterations (default: 200000).
- `timeout_ms`: reserved (not fully implemented).

## Results

`pe_equity_result_multi_t` includes:
- `results[i].equity`, `win_prob`, `tie_prob`, `ev`.
- `hilo_results[i]` for Hi/Lo games.
- `samples`: number of sampled matchups.
- `exact`: 1 if exhaustive, 0 if Monte Carlo.

## Error handling

All functions return `pe_status_t`. Use `pe_error_string()` for diagnostics:

```c
pe_status_t st = pe_equity_preflop(game_holdem, ranges, 2, &opts, &result);
if (st != PE_STATUS_OK) {
    fprintf(stderr, "Equity error: %s\n", pe_error_string(st));
}
```
