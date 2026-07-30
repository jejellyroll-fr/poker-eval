# Multiway Equity Guide

## Objective
`CalculateMultiwayEquity` evaluates weighted range equity in multiway pots (side pots, MC or exhaustive).

## C Engine

```c
#include <poker_eval/equity/RangeEquity.h>

MultiwayPotState state = {ranges, stack_sizes, invested, num_players};
MultiwayEquityOptions opts = {use_mc, iterations, orderflag};
MultiwayEquityResult res;
int matchups = CalculateMultiwayEquity(game_holdem, &state, board, dead,
                                      cards_to_deal, &opts, &res);
```

Each `PlayerRange` contains `hand_masks`, `weights` (optional), `count` (number of hands in the range), and `total_weight`. Side pots are calculated by `pe_calculate_sidepots`.

## Python

```python
from pokereval import PokerEval
pe = PokerEval()
res = pe.calculate_multiway_equity(
    "holdem",
    [["AA{60%}", "KK{40%}"], ["QQ"], ["JJ"]],
    [120, 80, 80],
    board_card_strings=["Th", "9d", "4c"],
    use_montecarlo=True,
    iterations=5000
)
```
Result: `{ 'matchups': ..., 'total_weighted_samples': ..., 'players': [{'ev':...,'equity':...}] }`.

## Weighting & Exclusions

- `{}` syntax for weights (decimal or `%`) – automatically normalized.
- Combined with `!` for targeted exclusions (`AA, !AsAh`).

## Benchmarks & Tests

- `tests/test_multiway_equity.c` (side pot, tie, weighted MC).
- `src/examples/bench_multiway_equity.c`: compares exhaustive vs Monte Carlo.


