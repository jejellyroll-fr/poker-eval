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
`down` means the hole card is unknown:

```python
pe.soko_street_equity(
    [{"up": ["Kh", "Qh"], "down": ["Ah"]},   # third street
     {"up": ["Kd", "Qd"]}],                  # hole card unknown
    iterations=20000,
    seed=7,                                    # reproducible sampling
)
# {'hi': [0.62..., 0.37...], 'exact': 0, 'samples': 20000}
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
