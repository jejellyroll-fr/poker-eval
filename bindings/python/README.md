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
