# Scripts Directory

This directory contains all the scripts for building, testing, and benchmarking the `poker-eval` library.

## Directory Structure

```
scripts/
├── run.sh                          # Main script launcher
├── build.sh                        # Standard build script
├── build_gpu_tests_only.sh         # Build GPU tests only
├── build_linux_clang.sh            # Linux build using Clang
├── build_linux_gcc.sh              # Linux build using GCC
├── build_macos_clang.sh            # macOS build using Clang
├── build_windows_msvc.sh           # Windows build using MSVC
├── clean.sh                        # Clean build artifacts
├── generate_coverage_report.sh     # Generate test coverage reports
├── benchmarks/                     # Performance benchmarking scripts
│   ├── benchmark_batched_mc.sh
│   ├── benchmark_gpu.sh
│   ├── benchmark_gpu_comprehensive.sh
│   ├── benchmark_simd.sh
│   └── profile_mt_versions.sh
└── tests/                          # Test execution scripts
    ├── run_joker_tests.sh
    ├── run_lowball_tests.sh
    ├── run_tests_coverage.sh
    └── validate_gpu_implementation.sh
```

## Usage

The main entry point is `run.sh` which provides a unified interface to all scripts:

```bash
# Show help
./scripts/run.sh help

# Run benchmarks
./scripts/run.sh benchmark gpu        # GPU benchmarks
./scripts/run.sh benchmark batched    # Batched Monte Carlo
./scripts/run.sh benchmark simd       # SIMD optimizations
./scripts/run.sh benchmark mt         # Multi-threading
./scripts/run.sh benchmark all        # All benchmarks

# Run tests
./scripts/run.sh test joker          # Joker deck tests
./scripts/run.sh test lowball        # Lowball tests
./scripts/run.sh test gpu            # GPU validation
./scripts/run.sh test all            # All tests

# Build operations
./scripts/build.sh                   # Standard project build
./scripts/clean.sh                   # Clean build artifacts
./scripts/generate_coverage_report.sh # Generate test coverage
```

## Build Scripts

- **`build.sh`**: Standard build script for CMake-based builds.
- **`build_gpu_tests_only.sh`**: Build target for GPU test suite validation.
- **`build_linux_gcc.sh` / `build_linux_clang.sh`**: Platform-specific build scripts for Linux.
- **`build_macos_clang.sh`**: macOS Clang build setup.
- **`build_windows_msvc.sh`**: Windows MSVC build script.
- **`clean.sh`**: Removes build output directories and temporary artifacts.
- **`generate_coverage_report.sh`**: Runs tests and generates gcov/lcov coverage reports.

## Individual Scripts

### Benchmarks

- **`benchmark_batched_mc.sh`**: Tests the batched Monte Carlo implementation performance
- **`benchmark_gpu.sh`**: Basic GPU performance tests
- **`benchmark_gpu_comprehensive.sh`**: Comprehensive GPU benchmarks with various batch sizes
- **`benchmark_simd.sh`**: SIMD optimization benchmarks
- **`profile_mt_versions.sh`**: Profiles different multi-threading implementations

### Tests

- **`run_joker_tests.sh`**: Runs all joker deck related tests
- **`run_lowball_tests.sh`**: Runs lowball game tests
- **`run_tests_coverage.sh`**: Runs test suite with code coverage tracking
- **`validate_gpu_implementation.sh`**: Comprehensive GPU implementation validation

## Requirements

- Bash shell
- GCC / Clang / MSVC compiler with OpenMP support
- CMake (3.15+)
- OpenCL / CUDA (for GPU features)
- Standard Unix tools (`grep`, `awk`, `sed`, `bc`, etc.)