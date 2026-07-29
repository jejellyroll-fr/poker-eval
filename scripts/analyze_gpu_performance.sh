#!/bin/bash
# analyze_gpu_performance.sh - Analyze GPU benchmark results

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESULTS_DIR="$PROJECT_ROOT/benchmark_results/gpu"

if [ ! -d "$RESULTS_DIR" ]; then
    echo "⚠ No benchmark results found"
    echo "Run ./scripts/run_gpu_benchmarks.sh first"
    exit 1
fi

# Find most recent results
LATEST_MAIN=$(ls -t "$RESULTS_DIR"/main_*.txt 2>/dev/null | head -1)
LATEST_COMP=$(ls -t "$RESULTS_DIR"/comprehensive_*.txt 2>/dev/null | head -1)
LATEST_SIMD=$(ls -t "$RESULTS_DIR"/simd_comparison_*.txt 2>/dev/null | head -1)

if [ -z "$LATEST_MAIN" ]; then
    echo "⚠ No benchmark results found"
    exit 1
fi

echo "═══════════════════════════════════════════════════════════════"
echo "          GPU PERFORMANCE ANALYSIS REPORT"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Analyzing results from: $(basename $LATEST_MAIN)"
echo ""

# Extract key metrics from main benchmark
echo "=== Main GPU Benchmark Results ==="
echo ""

# GPU device info
echo "GPU Device:"
grep -A 5 "GPU Device" "$LATEST_MAIN" 2>/dev/null || echo "  Not found"
echo ""

# Performance metrics
echo "Performance Metrics:"
grep -E "Throughput|Speedup|sims/sec" "$LATEST_MAIN" 2>/dev/null | head -10
echo ""

# Parse throughput
THROUGHPUT=$(grep "Throughput" "$LATEST_MAIN" 2>/dev/null | head -1 | awk '{print $NF}' | sed 's/[^0-9.]//g')
if [ -n "$THROUGHPUT" ]; then
    echo "Peak Throughput: $THROUGHPUT sims/sec"
fi

# Parse speedup
SPEEDUP_CPU=$(grep "Speedup vs CPU" "$LATEST_MAIN" 2>/dev/null | head -1 | awk '{print $4}' | sed 's/x//g')
if [ -n "$SPEEDUP_CPU" ]; then
    echo "Speedup vs CPU: ${SPEEDUP_CPU}x"

    # Check if meets target
    TARGET=1000
    if awk "BEGIN {exit !($SPEEDUP_CPU >= $TARGET)}"; then
        echo "✓ MEETS TARGET (>= ${TARGET}x)"
    else
        echo "⚠ BELOW TARGET (< ${TARGET}x)"
        echo "  Optimization needed"
    fi
fi
echo ""

# Analyze comprehensive tests if available
if [ -n "$LATEST_COMP" ]; then
    echo "=== Comprehensive Test Results ==="
    echo ""

    # Count passed/failed tests
    TOTAL_TESTS=$(grep -c "✓\|✗" "$LATEST_COMP" 2>/dev/null || echo "0")
    PASSED_TESTS=$(grep -c "✓" "$LATEST_COMP" 2>/dev/null || echo "0")
    FAILED_TESTS=$(grep -c "✗" "$LATEST_COMP" 2>/dev/null || echo "0")

    echo "Test Results: $PASSED_TESTS/$TOTAL_TESTS passed"

    if [ "$FAILED_TESTS" -gt 0 ]; then
        echo "⚠ Failed tests:"
        grep "✗" "$LATEST_COMP" | head -10
    fi
    echo ""

    # Extract timing summary
    echo "Performance Summary:"
    grep "Total time:" "$LATEST_COMP" 2>/dev/null || echo "  Not found"
    echo ""
fi

# Compare with SIMD if available
if [ -n "$LATEST_SIMD" ]; then
    echo "=== GPU vs SIMD Comparison ==="
    echo ""

    SIMD_SPEEDUP=$(grep "Overall speedup" "$LATEST_SIMD" 2>/dev/null | awk '{print $3}' | sed 's/x//g')
    if [ -n "$SIMD_SPEEDUP" ] && [ -n "$SPEEDUP_CPU" ]; then
        GPU_VS_SIMD=$(awk "BEGIN {print $SPEEDUP_CPU / $SIMD_SPEEDUP}")
        echo "GPU is ${GPU_VS_SIMD}x faster than SIMD"

        if awk "BEGIN {exit !($GPU_VS_SIMD >= 10)}"; then
            echo "✓ Excellent GPU acceleration"
        elif awk "BEGIN {exit !($GPU_VS_SIMD >= 5)}"; then
            echo "✓ Good GPU acceleration"
        else
            echo "⚠ GPU advantage over SIMD is modest"
        fi
    fi
    echo ""
fi

# Generate recommendations
echo "=== Recommendations ==="
echo ""

if [ -n "$SPEEDUP_CPU" ]; then
    if awk "BEGIN {exit !($SPEEDUP_CPU < 1000)}"; then
        echo "1. GPU performance below 1000x target"
        echo "   → Review kernel optimization"
        echo "   → Check memory transfer overhead"
        echo "   → Verify batch sizes are optimal"
        echo ""
    fi
fi

if [ "$FAILED_TESTS" -gt 0 ]; then
    echo "2. Some tests failed"
    echo "   → Review failed test details in:"
    echo "     $LATEST_COMP"
    echo ""
fi

echo "═══════════════════════════════════════════════════════════════"
echo "Full details available in:"
echo "  $LATEST_MAIN"
if [ -n "$LATEST_COMP" ]; then
    echo "  $LATEST_COMP"
fi
if [ -n "$LATEST_SIMD" ]; then
    echo "  $LATEST_SIMD"
fi
echo "═══════════════════════════════════════════════════════════════"
