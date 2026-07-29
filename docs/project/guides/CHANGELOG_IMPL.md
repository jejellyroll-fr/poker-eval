# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
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

See docs/ChangeLog.md for historical changes.