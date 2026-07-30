# Testing Guide - Poker-Eval

This guide explains how to run and manage project tests.

## Building and Running Tests

The `tests/` directory is directly configured in the main `CMakeLists.txt` at line 480 (`add_subdirectory(tests)` is enabled when the `BUILD_TESTS` variable is set).

### Enable and Run the Test Suite

By default or during CMake configuration, set `BUILD_TESTS=ON` to compile all unit and integration tests:

```bash
mkdir -p build
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc 2>/devnull || sysctl -n hw.ncpu)

# Run all tests via CTest
ctest --output-on-failure
```

### Targeted Execution of Test Executables

Once compiled, you can directly run the test executables located in `build/tests/`:

```bash
./tests/test_card_unity              # Basic Unity tests
./tests/test_holdem                  # Texas Hold'em
./tests/test_omaha_simple            # Omaha
./tests/test_badugi                  # Badugi
./tests/joker_eval_test              # Joker
./tests/test_range_equity_mt         # Range Equity MT
./tests/test_icm                     # ICM
```

### Execution by Category with CTest

```bash
# Core Tests
ctest -L core --output-on-failure

# Equity Tests
ctest -L equity --output-on-failure

# Range Tests
ctest -L range --output-on-failure

# Engine Tests
ctest -L engine --output-on-failure

# Betting Tests
ctest -L betting --output-on-failure
```

## Examples and Benchmarks

In addition to the unit test suite, the project provides:

1. **Usage examples** (in `src/examples/`)
2. **Performance benchmarks** (in `src/benchmarks/` and `bin/`)

Benchmark execution examples:
```bash
./bin/bench_7c_simd_micro
./bin/bench_cfr_holdem_river
```

