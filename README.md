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

### Core evaluation and equity

| Guide | Description |
|-------|-------------|
| [Modern API Guide](docs/api/MODERN_API_GUIDE.md) | Entry point for the unified C API (`pe_*` functions) |
| [Equity API](docs/api/guides/PE_EQUITY_API_GUIDE.md) | Range-vs-range equity with the public `pe_*` interface |
| [Preflop & Flop Equity](docs/equity/USER_GUIDE_PREFLOP_FLOP.md) | Preflop tables, flop texture analysis, and equity queries |
| [Equity API Reference](docs/equity/API_REFERENCE.md) | Full reference for the preflop and flop equity modules |
| [Multiway Equity](docs/equity/guides/MULTIWAY_EQUITY_GUIDE.md) | Weighted multiway equity with side-pot support (Monte Carlo and exhaustive) |
| [Preflop Equity Usage](docs/equity/guides/PREFLOP_EQUITY_USAGE.md) | Practical preflop equity examples |
| [Betting Engine API](docs/api/guides/BETTING_API_GUIDE.md) | Query legal actions, validate bets, inspect sizing constraints |

### Range parsing

| Guide | Description |
|-------|-------------|
| [Range Syntax Reference](docs/parsing/guides/RANGE_SYNTAX.md) | Full syntax for hand ranges (`AKs+`, `77-55`, `%30`, combos) |
| [Advanced Range Parser](docs/parsing/guides/ADVANCED_RANGE_PARSER_GUIDE.md) | C API for parsing and manipulating poker ranges |
| [Omaha Range Parser](docs/parsing/guides/OMAHA_RANGE_PARSER_GUIDE.md) | PLO-specific range syntax (suit filters, rundowns, patterns) |
| [Hand Distributions](docs/parsing/guides/HAND_DISTRIBUTION_README.md) | Weighted hand distribution support |

### Game variants

| Guide | Description |
|-------|-------------|
| [PLO Nomenclature](docs/games/guides/PLO_NOMENCLATURE.md) | Pot-Limit Omaha hand classification and naming |
| [PLO Nomenclature Usage](docs/games/guides/PLO_NOMENCLATURE_USAGE.md) | Practical examples of PLO hand categorisation |
| [Badugi Strategy](docs/games/guides/BADUGI_STRATEGY_GUIDE.md) | Draw analysis and exchange strategy with `pokenum` |
| [Pineapple Hold'em](docs/games/guides/PINEAPPLE_USAGE.md) | 3-card Pineapple with automatic optimal discard |
| [Joker Games](docs/games/guides/JOKER_USAGE_GUIDE.md) | 53-card deck support (`Xx` wild card) |

### CFR solver

| Guide | Description |
|-------|-------------|
| [CFR Documentation Suite](docs/cfr/guides/README.md) | Overview and index for the multiway postflop CFR adapter |
| [4-Way Postflop Example](examples/4way_postflop/README.md) | End-to-end walkthrough: build a tree, run CFR, export results |
| [Heads-Up River Example](examples/heads_up_river/README.md) | Two-player river spot with JSON/CSV export and EV aggregation |

### ICM and tournament economics

| Guide | Description |
|-------|-------------|
| [ICM Calculator](docs/economics/guides/ICM_CALCULATOR_GUIDE.md) | Independent Chip Model calculations for tournament equity |

### GPU acceleration

| Guide | Description |
|-------|-------------|
| [GPU Acceleration](docs/gpu/guides/GPU_ACCELERATION_GUIDE.md) | Architecture overview (CUDA / OpenCL) |
| [GPU Usage](docs/gpu/guides/GPU_USAGE_GUIDE.md) | Getting started with GPU-accelerated evaluation |
| [GPU Batched Evaluation](docs/gpu/guides/GPU_BATCHED_EVALUATION.md) | High-throughput batched hand evaluation on GPU |
| [GPU Multi-Game Quickstart](docs/gpu/guides/GPU_MULTI_GAME_QUICKSTART.md) | Running multiple game types on GPU in a single session |

### Performance and optimisation

| Guide | Description |
|-------|-------------|
| [Performance Guide](docs/optimization/guides/PERFORMANCE_GUIDE.md) | Benchmarking and profiling the range parser |
| [SIMD Vectorisation](docs/optimization/guides/SIMD_USAGE_GUIDE.md) | SSE/AVX hand evaluation for parallel throughput |
| [Multithreading](docs/optimization/guides/MULTITHREADING_USAGE_GUIDE.md) | Thread-safe equity calculation with work partitioning |

### Build and project

| Guide | Description |
|-------|-------------|
| [CMake Build Guide](docs/api/guides/CMAKE_BUILD_GUIDE.md) | Detailed build options, presets, cross-compilation |
| [Testing Guide](docs/testing/guides/TESTS_GUIDE.md) | Running the test suite and writing new tests |
| [Changelog](docs/project/guides/CHANGELOG_IMPL.md) | Implementation changelog |
| [Authors](docs/project/guides/AUTHORS.md) | Contributors |

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
