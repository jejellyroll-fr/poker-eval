#!/bin/bash

# Profile MT versions
echo "=== Profiling MT Optimization Versions ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "CPU: $(lscpu | grep 'Model name' | cut -d':' -f2 | xargs)"
echo "Cores: $(nproc)"
echo ""

cd examples

# Run benchmark with different thread counts
for threads in 1 2 4 8; do
    echo "=== Testing with OMP_NUM_THREADS=$threads ==="
    export OMP_NUM_THREADS=$threads
    
    # Run small test
    echo "Running small test..."
    ./test_mt_optimization 2>/dev/null | grep -E "(Time:|Speedup:|Improvement)"
    echo ""
done

# Run comprehensive benchmark
echo "=== Running comprehensive benchmark ==="
export OMP_NUM_THREADS=$(nproc)
./benchmark_all_versions 2>/dev/null | tail -50

# Memory usage analysis
echo ""
echo "=== Memory Usage Analysis ==="
echo "Checking memory usage for large ranges..."

# Use time command to measure memory
/usr/bin/time -v ./benchmark_large_ranges 2>&1 | grep -E "(Maximum resident|Elapsed)"

echo ""
echo "=== Profile Complete ==="