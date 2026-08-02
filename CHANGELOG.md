# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.1] - 2026-08-02

### Fixes
- Ace-to-five low evaluation on six and seven cards no longer refuses a no-pair
  low when a rank appears twice. A stud hand such as 4-5-5-6-6-7-8 now plays its
  8-7-6-5-4 instead of being scored as a pair. Affected `pe_eval_low_a5` and
  every caller: razz, 7studnsq, and the hi/lo games through the Python binding.
- `_findBestLowballHand` now packs the selected cards highest-first, matching the
  five-card path and `LowHandVal`'s layout. Two lows sharing their bottom cards
  were previously compared in the wrong order (A-2-3-4-8 beat A-2-3-5-6).
- `pe_low_qualify5` rejects paired hands. It only inspected `TOP_CARD`, so a low
  holding a pair of deuces passed the 8-or-better qualifier.
- The Python binding evaluates hi/lo lows with `StdDeck_Lowball8_EVAL`, as the
  library itself does, instead of the ace-to-five evaluator plus a qualifier.
  `winners()` awarded the low half of `7stud8` and `holdem8` pots to hands with
  no qualifying low.

## [1.1.0] - 2026-07-30

### Documentation & Translation
- Audited and updated 30+ Markdown documentation files across API, equity, games, GPU, optimization, parsing, testing, and source modules.
- Synchronized all API code snippets, struct definitions, enum values, and header paths (`<poker_eval/...>`) with C codebase contracts.
- Translated all French markdown documentation, code comments, prose, and section titles into technical English.

### Fixes & Stability
- **Memory Safety**: Added bounds checks to `strcpy`, `strcat`, `strlen` (using `strnlen`), and guaranteed NUL termination for `strncpy`.
- **Memory Safety**: Replaced `sprintf` with bounded formatting and used `sizeof(destination)` for `memcpy` bounds.
- **Portability**: Restored MSVC and MinGW compiler portability.
- **Concurrency**: Eliminated Linux solver race conditions and crash regressions.
- **Evaluators**: Corrected Debug assertion failures across evaluators and reported best five cards from stable C API.
- **Python Bindings**: Required Python >= 3.11 for wheel builds and updated packaging metadata.
- **CI / CMake**: Improved CMake preset resolution (`cmake.yml`) and enabled GPU tree compilation in CI.

## [1.0.0] - 2024-07-29

### Added
- Modernized CMake build system with modular target definitions.
- Installable Python bindings via `pyproject.toml` and `scikit-build-core`.
- GPU acceleration support (CUDA/OpenCL) for batched evaluation.
- SIMD optimizations for batch card manipulation and evaluation.
- Multi-threaded Monte Carlo simulation engine.
- Comprehensive Unity unit testing framework.
