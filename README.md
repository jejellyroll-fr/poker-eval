# poker-eval

A C library for poker hand evaluation, equity calculation, and game-tree
solving. Supports Hold'em, Omaha (PLO4/PLO5/PLO6), Stud, draw, lowball,
Badugi, Pineapple, mixed games, and Joker variants. Ships with a Python
wheel (`pokereval.PokerEval`) that drops in as a pip-installable package.

## Quick start

### C / CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Python

```bash
pip install .          # builds the native wheel
```

```python
from pokereval import PokerEval

pe = PokerEval()
result = pe.poker_eval(
    game="holdem",
    pockets=[["As", "Ah"], ["Ks", "Kh"]],
    board=["2c", "3d", "4h", "5s", "9c"],
)
print(result["eval"][0]["ev"])  # 1000
```

The wrapper retains the methods used by fpdb: `poker_eval`, `best`,
`card2string`, and `winners`.

## Features and documentation

### CFR solver

| Guide | Description |
|-------|-------------|
| [CFR Documentation Suite](docs/cfr/guides/README.md) | Overview and index for the multiway postflop CFR adapter |
| [4-Way Postflop Example](examples/4way_postflop/README.md) | End-to-end walkthrough: build a tree, run CFR, export results |
| [Heads-Up River Example](examples/heads_up_river/README.md) | Two-player river spot with JSON/CSV export and EV aggregation |

## Build options

| Option | Default | Purpose |
|--------|---------|---------|
| `BUILD_TESTS` | `ON` | Regression and integration tests |
| `BUILD_BINDINGS` | `ON` | Language bindings |
| `BUILD_PYTHON_BINDING` | `ON` | `pypokereval` + `pokereval.py` wrapper |
| `BUILD_C_API` | `ON` | Stable C API |
| `BUILD_EXAMPLES` | `ON` | Examples and CLI tools |
| `BUILD_GPU` | `ON` | GPU support (CUDA / OpenCL when available) |
| `POKER_EVAL_EXPERIMENTAL` | `OFF` | Experimental equity API (`poker_eval_calculate_equity*`) |

GPU support requires the platform SDK and can be disabled with `-DBUILD_GPU=OFF`.

The experimental Modern Equity API (`src/equity/poker_eval_modern_equity.c`,
header `include/poker_eval/core/poker_eval_modern.h`) is compiled only when
`-DPOKER_EVAL_EXPERIMENTAL=ON` is passed. It is still evolving, so its API is
not yet covered by the stable-API guarantees.

## CMake installation

```bash
cmake --install build --prefix /desired/prefix
```

Consumers can then use the exported CMake package:

```cmake
find_package(poker-eval CONFIG REQUIRED)
```

Public headers live under `include/poker_eval/`.

## Repository layout

```text
include/       Public headers
src/           Core library (evaluation, equity, games, GPU, parsing)
bindings/      C, Python, and optional language bindings
tests/         Regression and integration tests (Unity framework)
examples/      Focused usage examples (4-way postflop, heads-up river)
docs/          User guides, API references, and tutorials
scripts/       Build, validation, and benchmark helpers
```

## License

See `LICENCE` for the historical GPLv3-or-later licensing record and `LICENSE`
for the terms covering newer project material.
