#!/bin/bash
# Generate CFR shortdeck_NAME river report

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPORTS_DIR="$ROOT_DIR/docs/reports"

# Find the bench binary
BENCH_BIN=""
for BUILD_DIR in "$ROOT_DIR/build_cli" "$ROOT_DIR/build/legacy" "$ROOT_DIR/build_local" "$ROOT_DIR/build_x64" "$ROOT_DIR/build"; do
    if [ -f "$BUILD_DIR/bin/bench_cfr_shortdeck_river" ]; then
        BENCH_BIN="$BUILD_DIR/bin/bench_cfr_shortdeck_river"
        break
    fi
done

if [ -z "$BENCH_BIN" ]; then
    echo "Error: bench_cfr_shortdeck_river not found in build directories"
    exit 1
fi

mkdir -p "$REPORTS_DIR"

# Generate dealset
echo "Generating shortdeck_NAME dealset..."
python3 "$ROOT_DIR/scripts/generate_shortdeck_dealset.py" 20 12345 > "$REPORTS_DIR/cfr_shortdeck_dealset.txt"

# Run benchmark
echo "Running CFR shortdeck_NAME river benchmark..."
DEALS=${CFR_DEALS:-20}
ITERS=${CFR_ITERS:-1000}
BUCKET_MODE=${CFR_BUCKET_MODE:-3}

$BENCH_BIN \
    --deals $DEALS \
    --iters $ITERS \
    --dealset "$REPORTS_DIR/cfr_shortdeck_dealset.txt" \
    --bucket-mode $BUCKET_MODE \
    --csv "$REPORTS_DIR/cfr_shortdeck_report.csv"

echo "CFR shortdeck_NAME report generated: $REPORTS_DIR/cfr_shortdeck_report.csv"
