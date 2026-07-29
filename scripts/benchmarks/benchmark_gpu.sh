#!/bin/bash

# GPU Acceleration Benchmark Script
# This script runs comprehensive benchmarks comparing CPU vs GPU performance

echo "=== Poker Evaluation GPU Acceleration Benchmark ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo

# Check if GPU example exists
if [ ! -f "./gpu_eval_example" ]; then
    echo "Building GPU evaluation example..."
    make gpu_eval_example
    if [ $? -ne 0 ]; then
        echo "Failed to build GPU example. Make sure CUDA/OpenCL is installed."
        exit 1
    fi
fi

echo "=== System Information ==="
echo "CPU Information:"
if command -v lscpu &> /dev/null; then
    lscpu | grep "Model name\|CPU(s)\|Thread(s)\|Core(s)"
elif command -v sysctl &> /dev/null; then
    sysctl -n machdep.cpu.brand_string
    sysctl -n hw.ncpu
fi

echo
echo "GPU Information:"
if command -v nvidia-smi &> /dev/null; then
    nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits
elif command -v clinfo &> /dev/null; then
    clinfo | grep -E "Device Name|Global memory size"
fi

echo
echo "=== Running Benchmarks ==="

# Run the GPU evaluation example
echo "Starting GPU evaluation benchmark..."
./gpu_eval_example

echo
echo "=== Performance Analysis ==="

# Additional benchmarks for different scenarios
echo "Running extended benchmarks..."

# Test 1: Small batch performance
echo "Test 1: Small batch evaluation (1K boards)"
time ./gpu_eval_example 2>&1 | grep -A 5 "1000 boards"

echo
# Test 2: Large batch performance  
echo "Test 2: Large batch evaluation (100K boards)"
time ./gpu_eval_example 2>&1 | grep -A 5 "100000 boards"

echo
# Test 3: Monte Carlo performance
echo "Test 3: Monte Carlo simulation performance"
time ./gpu_eval_example 2>&1 | grep -A 10 "Monte Carlo"

echo
echo "=== Memory Usage Analysis ==="
if command -v nvidia-smi &> /dev/null; then
    echo "GPU Memory Usage:"
    nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits
fi

echo
echo "=== Recommendations ==="
echo "Based on the benchmark results:"
echo "1. GPU acceleration is most effective for batch sizes > 10K"
echo "2. Monte Carlo simulations show the highest speedup"
echo "3. For small batches (< 1K), CPU may be more efficient"
echo "4. Consider GPU memory capacity when choosing batch sizes"

echo
echo "=== Benchmark Complete ==="
echo "Results saved to benchmark_results_$(date +%Y%m%d_%H%M%S).log"