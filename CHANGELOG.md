# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
