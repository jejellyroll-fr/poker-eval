# Poker-Eval Modular Structure

This new structure organizes the code into logical modules to improve maintainability and reusability.

## Architecture

```
src/
├── benchmarks/    # Performance benchmarks
├── core/          # Core functionality
├── distributions/ # Hand distribution management
├── economics/     # Economic calculations, pot odds, and EV
├── engine/        # Game engine and state machines
├── equity/        # Equity calculations and enumeration
├── examples/      # API usage examples
├── games/         # Game-specific logic
├── gpu/           # GPU acceleration (CUDA / OpenCL)
├── ofc/           # Open-Face Chinese Poker (OFC)
├── range/         # Range parsing and operations
└── utils/         # Utilities and helper functions
```

## Modules

### Benchmarks (`src/benchmarks/`)
The benchmarks module contains performance tests:
- 7-card SIMD micro-benchmarks
- Evaluator and enumerator validation
- CFR solver benchmarks (Hold'em, Omaha, Stud, Razz, Short Deck)
- Preflop/flop and GPU equity benchmarks

### Core (`src/core/`)
The core module contains fundamental functionality:
- Core definitions (`poker_defs.h`)
- Card deck management (`deck_*.h/c`)
- Base evaluation (`handval.h`, `inlines/eval.h`, `eval_context.h`)
- Lookup tables and caching

### Distributions (`src/distributions/`)
The distributions module manages hand ranges and distributions:
- Omaha, Stud, and Hold'em distributions
- Range parsing and generation
- PLO nomenclature
- Card conversions

### Economics (`src/economics/`)
The economics module handles economic and tournament analysis:
- Independent Chip Model (ICM)
- Rake and pot structure calculations
- EV and pot odds analysis

### Engine (`src/engine/`)
The engine module contains game engine components:
- Game state and betting round management
- Decision engine and CFR solvers
- Game simulation

### Equity (`src/equity/`)
The equity module contains all equity calculations:
- Exhaustive and Monte Carlo enumeration
- Range vs. range equity calculations
- SIMD and multithreading optimizations
- Multiway equity acceleration

### Examples (`src/examples/`)
The examples module provides demonstration programs:
- Usage examples for the modern `pe_*` API
- Demos for hand evaluation and equity calculation
- GPU and multithreaded integration examples

### Games (`src/games/`)
The games module contains variant-specific logic:
- Game rules (`rules_*.h/c`)
- Game definitions (`game_*.h`)
- Specialized evaluations (`inlines/eval_*.h`)
- Lowball (A-5, 2-7) and Hi/Lo algorithms

### GPU (`src/gpu/`)
The gpu module handles hardware acceleration:
- CUDA and OpenCL kernels for hand evaluation
- Batched evaluation
- GPU CFR solver and associated benchmarks

### OFC (`src/ofc/`)
The ofc module is dedicated to Open-Face Chinese Poker:
- Fast OFC hand evaluation
- Royalties calculation and Fantasyland rules
- SIMD optimizations for OFC

### Range (`src/range/`)
The range module handles algebraic processing of ranges:
- Hold'em, Omaha, and Stud syntax parsing
- Combination operations (union, intersection, difference)
- Weighting and dead card filtering

### Utils (`src/utils/`)
The utils module contains helper utilities:
- Combination/permutation functions
- Micro-optimizations
- Table generation
- Wrappers and interfaces

## Compilation

To compile with the new modular structure:

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Modular Usage
```c
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/enumerate.h>
```

### Traditional Usage (Compatibility)
```c
#include "poker_defs.h"  // Redirects to the new structure
```

## Benefits

1. **Modularity**: Each module can be used independently
2. **Maintainability**: Logically organized code
3. **Reusability**: Modules can be reused in other projects
4. **Extensibility**: Easier to add new features
5. **Readability**: Clear and intuitive structure

## Migration

The new structure maintains backward compatibility with the existing API through:
- Library aliases (`poker_eval` → `poker_eval_modular`)
- Header redirections
- Preservation of public interfaces

## Testing

All existing tests continue to work with the new structure.
New tests can target specific modules.
