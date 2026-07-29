# Scripts Directory

This directory contains all the scripts for building, testing, and benchmarking the poker-eval library.

## Directory Structure

```
scripts/
├── run.sh              # Main script launcher
├── benchmarks/         # Performance benchmarking scripts
│   ├── benchmark_batched_mc.sh
│   ├── benchmark_gpu.sh
│   ├── benchmark_gpu_comprehensive.sh
│   ├── benchmark_simd.sh
│   └── profile_mt_versions.sh
├── tests/              # Test execution scripts
│   ├── run_joker_tests.sh
│   ├── run_lowball_tests.sh
│   └── validate_gpu_implementation.sh
└── build/              # Build-related scripts
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
./scripts/run.sh build               # Build the project
./scripts/run.sh build clean         # Clean and rebuild
./scripts/run.sh clean               # Clean build artifacts
```

## Individual Scripts

### Benchmarks

- **benchmark_batched_mc.sh**: Tests the batched Monte Carlo implementation performance
- **benchmark_gpu.sh**: Basic GPU performance tests
- **benchmark_gpu_comprehensive.sh**: Comprehensive GPU benchmarks with various batch sizes
- **benchmark_simd.sh**: SIMD optimization benchmarks
- **profile_mt_versions.sh**: Profiles different multi-threading implementations

### Tests

- **run_joker_tests.sh**: Runs all joker deck related tests
- **run_lowball_tests.sh**: Runs lowball game tests
- **validate_gpu_implementation.sh**: Comprehensive GPU implementation validation

## Requirements

- Bash shell
- GCC compiler with OpenMP support
- OpenCL (for GPU features)
- bc (for calculations in some scripts)
- Standard Unix tools (grep, awk, sed, etc.)