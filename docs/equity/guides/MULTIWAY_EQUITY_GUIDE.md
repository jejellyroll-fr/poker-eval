# Guide Multiway Equity

## Objectif
``CalculateMultiwayEquity`` évalue l'équité de ranges pondérées en multiway (side-pots, MC ou exhaustif).

## Pilier C

```
#include <poker_eval/equity/RangeEquity.h>

MultiwayPotState state = {ranges, stack_sizes, invested, num_players};
MultiwayEquityOptions opts = {use_mc, iterations, orderflag};
MultiwayEquityResult res;
int matchups = CalculateMultiwayEquity(game_holdem, &state, board, dead,
                                      cards_to_deal, &opts, &res);
```

Chaque `PlayerRange` porte `hand_masks`, `weights` (optionnel), `count` (nombre de mains dans la range) et `total_weight`. Side-pots calculés par `pe_calculate_sidepots`.

## Python

```
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
Résultat : `{ 'matchups': ..., 'total_weighted_samples': ..., 'players': [{'ev':...,'equity':...}] }`.

## Pondérations & exclusions

- Syntaxe `{}` pour poids (decimal ou `%`) – normalisées automatiquement.
- Combinaison avec `!` pour exclusions ciblées (`AA, !AsAh`).

## Bench & tests

- `tests/test_multiway_equity.c` (side pot, tie, MC pondéré).
- `src/examples/bench_multiway_equity.c` : compare exhaustif vs Monte Carlo.

