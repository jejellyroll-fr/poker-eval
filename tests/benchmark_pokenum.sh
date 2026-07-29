#!/bin/bash
# benchmark_pokenum.sh - Benchmark using pokenum to measure real-world impact

echo "=== Micro-Optimization Real-World Benchmark ==="
echo "Using pokenum to measure performance on realistic scenarios"
echo

POKENUM="../build/pokenum"
ITERATIONS=5

if [ ! -f "$POKENUM" ]; then
    echo "Error: pokenum not found at $POKENUM"
    echo "Please build the project first"
    exit 1
fi

echo "Each test will be run $ITERATIONS times"
echo

# Function to run a benchmark
run_benchmark() {
    local name="$1"
    local args="$2"
    local total_time=0
    
    echo "Test: $name"
    echo "Command: $POKENUM $args"
    
    for i in $(seq 1 $ITERATIONS); do
        # Run and capture time
        start=$(date +%s.%N)
        $POKENUM $args > /dev/null 2>&1
        end=$(date +%s.%N)
        
        # Calculate elapsed time
        elapsed=$(echo "$end - $start" | bc)
        total_time=$(echo "$total_time + $elapsed" | bc)
        
        printf "  Run %d: %.3f seconds\n" $i $elapsed
    done
    
    avg_time=$(echo "scale=3; $total_time / $ITERATIONS" | bc)
    echo "  Average: $avg_time seconds"
    echo
}

# Test 1: Heads-up preflop (most intensive)
run_benchmark "AA vs KK preflop" "-h As Ah - Ks Kh"

# Test 2: 3-way pot on flop
run_benchmark "3-way on flop" "-h As Ah - Ks Kh - Qs Qh -- 9s 8s 7h"

# Test 3: 4-way pot on turn
run_benchmark "4-way on turn" "-h As Ah - Ks Kh - Qs Qh - Js Jh -- 9s 8s 7h 6c"

# Test 4: Hi/Lo game
run_benchmark "Hi/Lo with low board" "-h8 As Ah - Ks Kh -- Ac 2d 3h"

# Test 5: Multiple evaluations (Monte Carlo)
run_benchmark "Monte Carlo 100k" "-mc 100000 -h As Ah - Ks Kh"

echo "=== Performance Analysis ==="
echo
echo "The micro-optimizations should provide:"
echo "  - 2-5% improvement from division lookup tables"
echo "  - 2-5% improvement from branch prediction hints"
echo "  - 3-5% improvement from table alignment (if successful)"
echo "  - Total expected improvement: 7-15% on enumeration-heavy workloads"
echo
echo "Note: Actual improvements depend on:"
echo "  - CPU architecture and cache sizes"
echo "  - Compiler optimization level"
echo "  - Specific poker scenario being evaluated"