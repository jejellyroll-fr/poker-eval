#!/bin/bash
# Benchmark runner for Advanced Range Parser

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Default to build directory if not specified, but check a few common locations
BUILD_DIR="$PROJECT_ROOT/build"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   Range Parser Benchmark Suite                             ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found: $BUILD_DIR"
    echo "Please run: ./build.sh first"
    exit 1
fi

# Build benchmark if needed
if [ ! -f "$BUILD_DIR/bin/bench_range_parsing" ]; then
    echo "🔨 Building benchmark..."
    cd "$BUILD_DIR"
    cmake --build . --target bench_range_parsing
fi

# Run benchmark
echo "🚀 Running benchmarks..."
echo ""

cd "$BUILD_DIR"

if [ "$1" = "quick" ]; then
    ./bin/bench_range_parsing quick
else
    ./bin/bench_range_parsing
fi

echo ""
echo "✅ Benchmark complete!"
