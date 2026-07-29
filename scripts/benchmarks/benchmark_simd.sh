#!/bin/bash

# SIMD Benchmark Script
# Compares performance of scalar vs SIMD implementations

echo "==================================="
echo "SIMD Performance Benchmark"
echo "==================================="
echo ""

# Check if binaries exist
if [ ! -f "./tests/test_simd_operations" ]; then
    echo "Error: test_simd_operations not found. Please build the project first."
    exit 1
fi

if [ ! -f "./simd_range_equity_example" ]; then
    echo "Error: simd_range_equity_example not found. Please build the project first."
    exit 1
fi

# Detect CPU features
echo "CPU Information:"
echo "----------------"
if [ -f /proc/cpuinfo ]; then
    echo -n "CPU Model: "
    grep -m 1 "model name" /proc/cpuinfo | cut -d: -f2 | xargs
    echo -n "CPU Cores: "
    grep -c "processor" /proc/cpuinfo
    echo -n "SIMD Support: "
    grep -o -E 'sse2|avx2|avx512' /proc/cpuinfo | sort -u | tr '\n' ' '
    echo ""
elif [ "$(uname)" == "Darwin" ]; then
    echo -n "CPU Model: "
    sysctl -n machdep.cpu.brand_string
    echo -n "CPU Cores: "
    sysctl -n hw.ncpu
    echo ""
fi
echo ""

# Run SIMD capability detection
echo "SIMD Capability Detection:"
echo "-------------------------"
./tests/test_simd_operations | grep -A1 "=== SIMD Capability"
echo ""

# Run performance tests
echo "Running Performance Tests..."
echo "============================"
echo ""

# Test 1: Basic SIMD operations test
echo "Test 1: SIMD Operations Validation"
echo "----------------------------------"
./tests/test_simd_operations | grep -E "Correctness Test|All .* hands evaluated|errors"
echo ""

# Test 2: Performance comparison
echo "Test 2: Performance Comparison"
echo "------------------------------"
./tests/test_simd_operations | grep -A10 "Performance Test"
echo ""

# Test 3: Range equity example
echo "Test 3: Range Equity Calculation"
echo "--------------------------------"
./simd_range_equity_example | grep -E "Detected SIMD|Performance Comparison" -A10
echo ""

# Test 4: Batch size efficiency
echo "Test 4: Batch Size Efficiency"
echo "-----------------------------"
./tests/test_simd_operations | grep -A20 "Batch Size Test"
echo ""

# Run multiple iterations for statistical significance
echo "Test 5: Statistical Performance Analysis"
echo "---------------------------------------"
echo "Running 5 iterations..."
echo ""

total_scalar=0
total_simd=0

for i in {1..5}; do
    echo -n "Iteration $i: "
    result=$(./tests/test_simd_operations | grep -A3 "Performance Test" | grep -E "Scalar time:|SIMD time:")
    scalar=$(echo "$result" | grep "Scalar time:" | awk '{print $3}')
    simd=$(echo "$result" | grep "SIMD time:" | awk '{print $3}')
    
    if [ ! -z "$scalar" ] && [ ! -z "$simd" ]; then
        echo "Scalar: ${scalar}s, SIMD: ${simd}s"
        total_scalar=$(echo "$total_scalar + $scalar" | bc)
        total_simd=$(echo "$total_simd + $simd" | bc)
    fi
done

# Calculate averages
if [ ! -z "$total_scalar" ] && [ ! -z "$total_simd" ]; then
    avg_scalar=$(echo "scale=3; $total_scalar / 5" | bc)
    avg_simd=$(echo "scale=3; $total_simd / 5" | bc)
    speedup=$(echo "scale=2; $avg_scalar / $avg_simd" | bc)
    
    echo ""
    echo "Average Results:"
    echo "  Scalar: ${avg_scalar}s"
    echo "  SIMD: ${avg_simd}s"
    echo "  Average Speedup: ${speedup}x"
fi

echo ""
echo "==================================="
echo "Benchmark Complete"
echo "==================================="