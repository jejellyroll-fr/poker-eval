#!/bin/bash

# Benchmark script for Batched Monte-Carlo implementation

echo "Building Batched Monte-Carlo benchmark..."

# Compile the batched Monte-Carlo library
gcc -c -O3 -march=native -ffast-math -I./include lib/batched_montecarlo.c -o lib/batched_montecarlo.o

# Create a static library including the new batched code
ar rcs libpoker_batched.a lib/*.o

# Compile the benchmark
gcc -O3 -march=native -ffast-math -I./include \
    examples/batched_montecarlo_example.c \
    -L. -lpoker_batched -lm \
    -o batched_mc_benchmark

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"
echo ""
echo "Running benchmarks..."
echo "===================="

# Run with different iteration counts
echo "Test 1: Quick test (10,000 iterations)"
./batched_mc_benchmark 10000

echo ""
echo "Test 2: Standard test (100,000 iterations)"
./batched_mc_benchmark 100000

echo ""
echo "Test 3: Large test (1,000,000 iterations)"
./batched_mc_benchmark 1000000

# Profile cache performance if perf is available
if command -v perf &> /dev/null; then
    echo ""
    echo "Cache performance analysis..."
    echo "============================"
    
    echo ""
    echo "Regular enumSample cache stats:"
    perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
        ./batched_mc_benchmark 100000 2>&1 | grep -E "(cache|L1-dcache)"
fi

echo ""
echo "Benchmark complete!"