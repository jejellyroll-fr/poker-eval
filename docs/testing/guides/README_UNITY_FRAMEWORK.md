# Unified Testing Framework with Unity

## Overview

This project now uses Unity as its unified test framework, allowing different types of tests to be organized and executed consistently.

## Test Structure

### 1. Unit Tests
- **File**: `test_card_unity.c`
- **Objective**: Test individual functions
- **Example**: Tests for functions `CharToRank`, `CharToSuit`, etc.

### 2. Integration Tests
- **Directory**: `unity_tests/`
- **File**: `test_integration_example.c`
- **Objective**: Test interaction between multiple components
- **Example**: Integration between cards and deck

### 3. Performance Tests / Benchmarks
- **File**: `test_benchmark_example.c`
- **Objective**: Measure performance of critical functions
- **Example**: Benchmark card conversions

## Installation and Setup

### Unity Installation

Unity will be automatically downloaded during the first build. If you want to install it manually:

```bash
# From the root directory of the project
cd tests
git clone https://github.com/ThrowTheSwitch/Unity.git unity
```

### Compilation
```bash
cd build
cmake .. -DBUILD_TESTS=ON
make test_card_unity test_integration_example test_benchmark_example
```

**Note**: If Unity is not present, CMake will download it automatically.

### Executing Unity Tests
```bash
# Run all Unity tests
ctest -R "test_.*_unity|test_integration_example|test_benchmark_example" -V

# Run a specific test
./tests/test_card_unity
./tests/test_integration_example
./tests/test_benchmark_example
```

## Adding New Tests

### 1. Create a new unit test

```c
#include "unity.h"
#include "YourModule.h"

void setUp(void) {}
void tearDown(void) {}

static void test_your_function(void) {
    TEST_ASSERT_EQUAL_INT(expected_value, your_function(input));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_your_function);
    return UNITY_END();
}
```

### 2. Add the test to CMakeLists.txt

```cmake
add_unity_test(test_name source_file.c)
```

## Available Unity Assertions

- `TEST_ASSERT_EQUAL_INT(expected, actual)`
- `TEST_ASSERT_EQUAL_CHAR(expected, actual)`
- `TEST_ASSERT_TRUE(condition)`
- `TEST_ASSERT_FALSE(condition)`
- `TEST_ASSERT_NULL(pointer)`
- `TEST_ASSERT_NOT_NULL(pointer)`
- `TEST_ASSERT_EQUAL_STRING(expected, actual)`

## Advantages of the Unity Framework

1. **Uniformity**: All tests use the same syntax
2. **Readability**: Clear and informative error messages
3. **Integration**: Compatible with CTest and CI/CD
4. **Performance**: Lightweight and fast framework
5. **Portability**: Works across all platforms

## Test Results

Unity tests display:
- The number of tests executed
- The number of passed/failed tests
- Failure details with line numbers
- Performance metrics for benchmarks

## Example Output

```
/path/to/test.c:43:test_char_to_rank:PASS
/path/to/test.c:44:test_char_to_suit:PASS
/path/to/test.c:45:test_rank_to_char:PASS
/path/to/test.c:46:test_suit_to_char:PASS

-----------------------
4 Tests 0 Failures 0 Ignored 
OK
```

## Migrating Existing Tests

Existing tests can be progressively migrated to Unity:
1. Keep existing tests functional
2. Create Unity equivalents for new features
3. Progressively migrate critical tests to Unity