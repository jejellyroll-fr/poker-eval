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

The library bundles two largely independent capabilities. They share the same
hand evaluator, range parser and game-variant layer, but they answer different
questions and are documented separately:

- **Equity calculation** — how much a hand or a range is worth against other
  ranges (win / tie / EV), on any street and for every supported variant.
- **Game-tree solving** — what the optimal (Nash) strategy is for a betting
  tree, via the CFR solver and its tooling.

### Equity calculation

| Resource | Description |
|----------|-------------|
| [Equity C API](include/poker_eval/equity.h) | Public `pe_equity_*` interface: range-vs-range, multiway (2–10 players), preflop |
| [Equity module](src/equity/README.md) | Enumeration engines, batched Monte Carlo, SIMD/multithreaded range equity, preflop tables, flop texture, Run It Twice, side pots |
| [Hand evaluation](src/core/README.md) | 5/7-card evaluation, SIMD, low/high, joker and short decks |
| [Python bindings](bindings/python/README.md) | `pokereval.PokerEval`: `poker_eval`, `calculate_range_equity`, `calculate_multiway_equity`, `winners`, `best` |

Equity command-line tools (built with `-DBUILD_EXAMPLES=ON`, output under
`build/src/examples/`):

| Tool | Description |
|------|-------------|
| `pokenum` | Hand- vs hand and range- vs range equity, exact or Monte Carlo (`-mc niter`), across all variants |
| `multiway_equity_cli` | Multiway equity with investment-weighted, side-pot-aware splits |
| `range_equity_calc` | Range-vs-range equity helper |

Tournament equity (`build/tools/pe-icm`) computes the Independent Chip Model
from stacks and payouts.

### Game-tree solving (CFR solver)

| Resource | Description |
|----------|-------------|
| [CFR Documentation Suite](docs/cfr/guides/README.md) | Overview and index for the multiway postflop CFR adapter |
| [4-Way Postflop Example](examples/4way_postflop/README.md) | End-to-end walkthrough: build a tree, run CFR, export results |
| [Heads-Up River Example](examples/heads_up_river/README.md) | Two-player river spot with JSON/CSV export and EV aggregation |
| [Heads-Up NLHE Trees](examples/nlhe_hu/README.md) | Preflop-to-river tree and three flop textures |
| [PLO5/PLO6 Trees](examples/plo_hu_streets/README.md) | Per-street trees for 5- and 6-card Omaha |

Solver command-line tools (output under `build/tools/`):

| Tool | Description |
|------|-------------|
| `mpf_run_with_metrics` | Load a tree, run CFR, stream metrics, save checkpoints and node maps |
| `mpf_dump_results` | Reload a run and export JSON/CSV result summaries |
| `pe-preflop-solve` | Solve preflop and postflop roots (`--street`, `--board`, `--pot`) |

Solver benchmarks and analytical regression oracles live under
[`benchmarks/solver/`](benchmarks/solver/README.md) and
[`tests/game_theory/`](tests/game_theory/README.md).

### Poker Eval Studio UI

To build the native desktop GUI (Poker Eval Studio):

```bash
./build_studio.sh
./build-studio/tools/poker-eval-studio
```

### Shared foundations

| Resource | Description |
|----------|-------------|
| [Range parsing](include/poker_eval/range.h) | Hold'em / Omaha / Stud range syntax and combination operations |
| [Hand distributions](src/distributions/README.md) | Weighted distributions, PLO nomenclature, card conversions |
| [Game variants](src/games/README.md) | Per-variant rules: Omaha, Stud, draw, lowball, Badugi, Pineapple, Joker, mixed games |
| [Module map](src/README.md) | Full source layout (`src/`): core, equity, engine, games, gpu, ofc, range, utils |
| [Scripts](scripts/README.md) | Build, coverage and benchmark helpers |

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
examples/      Focused usage examples (solver trees for NLHE and PLO, CFR demos)
docs/          CFR solver guides and references
scripts/       Build, validation, and benchmark helpers
```

## License

See `LICENCE` for the historical GPLv3-or-later licensing record and `LICENSE`
for the terms covering newer project material.
