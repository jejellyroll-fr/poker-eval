#!/bin/bash
# run_gpu_benchmarks.sh - Execute all GPU benchmarks

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/linux-clang"
RESULTS_DIR="$PROJECT_ROOT/benchmark_results/gpu"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

mkdir -p "$RESULTS_DIR"

echo "═══════════════════════════════════════════════════════════════"
echo "            OFC GPU BENCHMARKING SUITE"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Timestamp: $TIMESTAMP"
echo "Results will be saved to: $RESULTS_DIR"
echo ""

# Run comprehensive test as benchmark
if [ -f "$BUILD_DIR/tests/test_ofc_gpu_comprehensive" ]; then
    echo "[1/3] Running comprehensive GPU test..."
    "$BUILD_DIR/tests/test_ofc_gpu_comprehensive" > "$RESULTS_DIR/comprehensive_${TIMESTAMP}.txt" 2>&1
    echo "      ✓ Saved to comprehensive_${TIMESTAMP}.txt"
elif [ -f "$PROJECT_ROOT/test_ofc_gpu_comprehensive" ]; then
    echo "[1/3] Running comprehensive GPU test (local build)..."
    "$PROJECT_ROOT/test_ofc_gpu_comprehensive" > "$RESULTS_DIR/comprehensive_${TIMESTAMP}.txt" 2>&1
    echo "      ✓ Saved to comprehensive_${TIMESTAMP}.txt"
else
    echo "[1/3] ⚠ test_ofc_gpu_comprehensive not found"
fi

# Run basic test for comparison
if [ -f "$BUILD_DIR/tests/test_ofc_gpu_basic" ]; then
    echo "[2/3] Running basic GPU test..."
    "$BUILD_DIR/tests/test_ofc_gpu_basic" > "$RESULTS_DIR/basic_${TIMESTAMP}.txt" 2>&1
    echo "      ✓ Saved to basic_${TIMESTAMP}.txt"
elif [ -f "$PROJECT_ROOT/test_ofc_gpu_basic" ]; then
    echo "[2/3] Running basic GPU test (local build)..."
    "$PROJECT_ROOT/test_ofc_gpu_basic" > "$RESULTS_DIR/basic_${TIMESTAMP}.txt" 2>&1
    echo "      ✓ Saved to basic_${TIMESTAMP}.txt"
else
    echo "[2/3] ⚠ test_ofc_gpu_basic not found"
fi

# Run SIMD benchmark for comparison
if [ -f "$BUILD_DIR/bin/ofc_simd_benchmark" ]; then
    echo "[3/3] Running SIMD benchmark for comparison..."
    "$BUILD_DIR/bin/ofc_simd_benchmark" > "$RESULTS_DIR/simd_comparison_${TIMESTAMP}.txt" 2>&1
    echo "      ✓ Saved to simd_comparison_${TIMESTAMP}.txt"
else
    echo "[3/3] ⚠ ofc_simd_benchmark not found"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "✅ Benchmarks complete!"
echo ""
echo "Results summary:"
ls -lh "$RESULTS_DIR"/*_${TIMESTAMP}.txt 2>/dev/null || echo "  No results generated"
echo ""
echo "Next step: Run ./scripts/analyze_gpu_performance.sh to analyze results"
echo "═══════════════════════════════════════════════════════════════"
