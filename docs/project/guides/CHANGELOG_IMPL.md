# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-08-26

### Performance & Numerical Behavior
- Reworked `pe_vec_sum` and `pe_vec_dot` as four-lane, branchless Neumaier
  reductions. The public API and `double` storage remain unchanged; results
  can differ in the low bits because the accumulation order is now explicit.
- FP32 storage for regrets and averages is not enabled by this change; it
  remains a separately measured, opt-in follow-up.

## [1.1.0] - 2026-07-30

### Documentation & Translation
- Audited all 30+ markdown files across the repository against C source code headers.
- Synchronized all function signatures, struct fields, enum definitions, header include paths, and binary locations.
- Translated all French markdown documentation, code comments, and section titles into technical English.

### Fixes & Stability
- Bound memory operations across API boundaries (`strnlen`, `sizeof` bounds, NUL termination for `strncpy`).
- Restored MSVC and MinGW compiler portability.
- Fixed Linux solver race conditions and crash regressions.
- Corrected Debug assertion failures across evaluators.
- Fixed Python wheel build requirements (Python >= 3.11).
- Improved CI CMake preset resolution and GPU tree building.
- GPU acceleration support (CUDA/OpenCL)
- SIMD optimizations for batch operations
- Multi-threaded Monte Carlo simulations
- Batched Monte Carlo implementation
- Comprehensive test suite
- Joker deck support (53 cards)
- Short deck support (36 cards)
- Asian stud deck support (32 cards)
- Hand distribution parsing for Omaha and Stud
- Range equity calculation
- Evaluation cache for performance

### Changed
- Reorganized project structure
  - Scripts moved to `scripts/` directory
  - Documentation moved to `docs/` directory
  - Better separation of guides, implementation details, and reports
- Improved build system with CMake
- Enhanced Python bindings

### Fixed
- Double Flop Holdem evaluation in pokenum (both Monte Carlo and exhaustive enumeration)
- Win/lose/tie statistics now correctly calculated and displayed for double flop games
- Lowball algorithm corrections
- Memory management improvements
- Various bug fixes in hand evaluation

### Optimized
- Multi-threading with OpenMP
- SIMD operations for card manipulation
- GPU batch processing
- Cache-friendly data structures

## Previous Versions

See [ChangeLog.md](./ChangeLog.md) for historical changes.
