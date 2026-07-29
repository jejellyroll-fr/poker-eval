#!/bin/bash
# Generate CFR Omaha (high-only) river report

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPORTS_DIR="$ROOT_DIR/docs/reports"

# Find the bench binary
BENCH_BIN=""
for BUILD_DIR in "$ROOT_DIR/build_cli" "$ROOT_DIR/build/legacy" "$ROOT_DIR/build_local" "$ROOT_DIR/build_x64" "$ROOT_DIR/build"; do
    if [ -f "$BUILD_DIR/bin/bench_cfr_omaha_river" ]; then
        BENCH_BIN="$BUILD_DIR/bin/bench_cfr_omaha_river"
        break
    fi
done

if [ -z "$BENCH_BIN" ]; then
    echo "Error: bench_cfr_omaha_river not found in build directories"
    exit 1
fi

mkdir -p "$REPORTS_DIR"

# Generate dealset
echo "Generating Omaha dealset..."
python3 "$ROOT_DIR/scripts/generate_omaha_dealset.py" 20 12345 > "$REPORTS_DIR/cfr_omaha_dealset.txt"

# Run benchmark
echo "Running CFR Omaha river benchmark..."
DEALS=${CFR_DEALS:-20}
ITERS=${CFR_ITERS:-1000}
BUCKET_MODE=${CFR_BUCKET_MODE:-3}

$BENCH_BIN \
    --deals $DEALS \
    --iters $ITERS \
    --dealset "$REPORTS_DIR/cfr_omaha_dealset.txt" \
    --bucket-mode $BUCKET_MODE \
    --csv "$REPORTS_DIR/cfr_omaha_report.csv"

echo "CFR Omaha report generated: $REPORTS_DIR/cfr_omaha_report.csv"
