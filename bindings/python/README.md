# PyPoker-Eval (Python Bindings)

This package is a Python adapter for the `poker-eval` toolkit for writing programs which simulate or analyze poker games. The Python interface provides high-level abstractions for poker hand evaluation and equity calculations.

For details on the provided Python classes and functions, consult `pokereval.py`.

## Building and Installing Python Bindings

Python bindings can be built either via CMake from the repository root or installed directly using `pip`.

### Option 1: Install via pip

Navigate to the `bindings/python/` directory and run:

```bash
pip install .
```

For editable/development mode:

```bash
pip install -e .
```

### Option 2: Build with CMake from Repository Root

From the project root directory, configure CMake with `BUILD_BINDINGS=ON` and build:

```bash
mkdir -p build
cd build
cmake .. -DBUILD_BINDINGS=ON
cmake --build .
```

## Reproducible Monte Carlo

Sampling (`iterations=N`) draws from a thread-local PCG32 generator, not libc
`rand()`, so `srand()` has no effect. Seed it from Python instead:

```python
from pokereval import PokerEval
import pokereval

pe = PokerEval()
pockets = [["6d", "3h"], ["7s", "3s"]]

# Per call: the same arguments and seed give the same result,
# whatever ran before in the process.
pe.poker_eval(game="fusion", pockets=pockets, board=["__"] * 5,
              iterations=20000, seed=2)

# calculate_range_equity and calculate_multiway_equity take seed= too.

# For a whole run: set the base seed (also reseeds the calling thread).
pokereval.seed(2)          # or PokerEval.seed(2)
```

The stream is per thread: a call seeded in one thread does not move another
thread's stream. Exhaustive enumeration (no `iterations`) ignores the seed.

## Range equity intervals

`calculate_range_equity` reports, for each player, the batch-means standard
error of the range equity (`stderr`) and the 95% confidence half-width
derived from it (`ci95`, 1.96 x stderr): a sampled equity lies in
`ev ± ci95` with 95% confidence.  Each matchup with cards still to come is a
batch, so the interval measures the Monte Carlo noise of the range estimate,
not the spread of equity between matchups.

```python
pe.calculate_range_equity(
    "holdem",
    [["AsKs"], ["QdQh"]],
    board_card_strings=["2c", "7d", "9h"],
    use_montecarlo=True,
    iterations=6000,
    seed=5,
)
# {'info': (1, 0, 1),
#  'eval': [{'ev': 0.246...,
#            'stderr': 0.00556..., 'ci95': 0.01089..., ...}, ...]}
```

Both are 0 for an exhaustive call, and for hi/lo games: a low pot is a second
payoff correlated with the high one, which the per-side histograms used for the
variance cannot separate.  The interval is therefore defined for high-only
games.

## Prerequisites

- Python 3.7+
- C compiler (GCC, Clang, or MSVC)
- CMake 3.15+ (if building via CMake)

## Draw games

`poker_eval` and `winners` accept the draw games the core enumerates:

| Name | Game | Sides |
| --- | --- | --- |
| `5draw_std`, `5draw8_std`, `5drawnsq_std` | Five-card draw on the standard 52-card deck (high, hi/lo 8-or-better, hi/lo no qualifier) | hi, hi/lo |
| `5draw`, `5draw8`, `5drawnsq` | The same with a joker: a 53-card deck | hi, hi/lo |
| `27_triple_draw`, `a5_triple_draw` | 2-7 and A-5 lowball | low |
| `archie` | Archie triple draw hi/lo | hi/lo |
| `badugi`, `badugi_hilo` | Badugi, Badugi hi/lo | low, hi/lo |
| `drawmaha`, `drawmaha49`, `drawmaha_zero`, `drawmaha_dugi` | Omaha hand on the high side, five-card hand on the low side | hi/lo |
| `soko` | Sökö / Soko (Canadian Stud): five-card stud, four-card flush above four-card straight | hi |

With complete hands the joker and standard variants rank the same.  When
cards are still to come (`"__"` in a pocket), `5draw*` can deal the joker:
use the `_std` names for standard five-card draw.

### Sökö descriptions and street equity

`best()` returns a standard five-card value, which cannot express a four-card
flush or a four-card straight.  Use `describe_soko_hand()` for the Sökö
category and its tie-break ranks:

```python
pe.describe_soko_hand(["9h", "Th", "Jh", "3h", "2s"])
# {'category': 'FourCardFlush', 'category_rank': 3,
#  'ranks': [11, 10, 9, 3], 'rank_names': ['J', 'T', '9', '3'],
#  'value': ...}
```

The dict's `value` is the Sökö `HandVal`: larger always wins and equal values
tie, so `describe_soko_hand(a)["value"] > describe_soko_hand(b)["value"]`
compares two hands without inspecting their categories.

`soko_street_equity()` computes showdown equity at third, fourth or fifth
street; the street follows from the number of upcards, and an omitted
`down` means the hole card is unknown.  `iterations` reports the Monte Carlo
budget per matchup (0 when exact), `stderr` the standard error and `ci95` the
95% confidence half-width, so a sampled equity lies in `hi[i] ± ci95[i]`:

```python
pe.soko_street_equity(
    [{"up": ["Kh", "Qh"], "down": ["Ah"]},   # third street
     {"up": ["Kd", "Qd"]}],                  # hole card unknown
    iterations=20000,
    seed=7,                                    # reproducible sampling
)
# {'hi': [0.62..., 0.37...], 'exact': 0, 'samples': 1,
#  'iterations': 20000, 'stderr': [...], 'ci95': [...],
#  'required_iterations': None}
```

`target_ci` turns that interval into a budget.  The per-sample variance is
known analytically, so the half-width shrinks as `1/sqrt(iterations)` and the
iterations that reach a target half-width are `ceil(iterations ×
(ci95 / target_ci)²)`, reported per player as `required_iterations` (0 when
the interval is already empty):

```python
pe.soko_street_equity(
    [{"up": ["Kh", "Qh"], "down": ["Ah"]},
     {"up": ["Kd", "Qd"]}],
    iterations=2000,      # cheap pilot run
    target_ci=0.01,       # ... aiming at ±1 percentage point
)
# {'hi': [0.708..., 0.291...], 'exact': 0, 'samples': 1,
#  'iterations': 2000, 'stderr': [0.010, 0.010],
#  'ci95': [0.0199, 0.0199], 'required_iterations': [7904, 7904]}
```

```python
pe.winners(game="5draw_std",
           pockets=[["As", "Ks", "Qd", "7c", "2h"], ["Ah", "Kh", "Qc", "8d", "3s"]],
           board=[])
# {'hi': [1]}

# One card to come, sampled reproducibly:
pe.poker_eval(game="5draw_std",
              pockets=[["As", "Ad", "Kc", "__", "__"], ["Qs", "Qd", "Jc", "Jh", "2s"]],
              board=[], iterations=20000, seed=303)
```
