#!/usr/bin/env bash
# run_strat_grid_search.sh - Grid-search for optimal STRATIFIED sampling weights

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJ_ROOT"

# Default parameters
SCENARIO="${STRAT_SCENARIO:-flop}"
SAMPLES="${STRAT_SAMPLES:-5000}"
REPEATS="${STRAT_REPEATS:-10}"
W0_RANGE="${STRAT_W0_RANGE:-0.5,1.0,2.0}"
W1_RANGE="${STRAT_W1_RANGE:-1.0,1.5,2.0,2.5}"
W2_RANGE="${STRAT_W2_RANGE:-2.0,3.0,4.0}"
OUT_CSV="${STRAT_OUT_CSV:-strat_grid_search.csv}"
REPORTS_DIR="${REPORTS_DIR:-docs/reports}"

mkdir -p "$REPORTS_DIR"

echo "=== STRATIFIED Weights Grid-Search ==="
echo "Scenario: $SCENARIO"
echo "Samples: $SAMPLES, Repeats: $REPEATS"
echo "w0 (distinct) range: $W0_RANGE"
echo "w1 (onepair) range: $W1_RANGE"
echo "w2 (trips) range: $W2_RANGE"
echo "Output CSV: $OUT_CSV"
echo ""

# Run grid search
python3 scripts/grid_search_strat_weights.py \
    --scenario "$SCENARIO" \
    --samples "$SAMPLES" \
    --repeats "$REPEATS" \
    --w0-range "$W0_RANGE" \
    --w1-range "$W1_RANGE" \
    --w2-range "$W2_RANGE" \
    --out "$OUT_CSV"

if [ ! -f "$OUT_CSV" ]; then
    echo "ERROR: Grid search failed to produce $OUT_CSV"
    exit 1
fi

echo ""
echo "=== Generating Heatmaps ==="

# Generate heatmaps
python3 scripts/plot_strat_grid_heatmap.py \
    "$OUT_CSV" \
    --out-heatmap-w0 "$REPORTS_DIR/strat_heatmap_fix_w0.png" \
    --out-heatmap-w1 "$REPORTS_DIR/strat_heatmap_fix_w1.png" \
    --out-heatmap-w2 "$REPORTS_DIR/strat_heatmap_fix_w2.png" \
    --out-summary "$REPORTS_DIR/strat_weights_summary.png" \
    --title-prefix "$SCENARIO"

echo ""
echo "=== Extracting Best Weights ==="

# Find best weights and append to variance_summary.md
BEST_LINE=$(tail -n +2 "$OUT_CSV" | sort -t',' -k8 -n | head -1)
if [ -n "$BEST_LINE" ]; then
    BEST_W0=$(echo "$BEST_LINE" | cut -d',' -f1)
    BEST_W1=$(echo "$BEST_LINE" | cut -d',' -f2)
    BEST_W2=$(echo "$BEST_LINE" | cut -d',' -f3)
    BEST_RATIO=$(echo "$BEST_LINE" | cut -d',' -f8)

    echo "Best weights for scenario '$SCENARIO':"
    echo "  w=(${BEST_W0}, ${BEST_W1}, ${BEST_W2})"
    echo "  Variance ratio: $BEST_RATIO"
    echo "  Speedup: $(python3 -c "print(f'{1.0/float(\"$BEST_RATIO\"):.2f}')")× variance reduction vs MC"

    # Append to summary markdown
    SUMMARY_MD="$REPORTS_DIR/variance_summary.md"
    if [ -f "$SUMMARY_MD" ]; then
        echo "" >> "$SUMMARY_MD"
        echo "## Optimal STRATIFIED Weights ($SCENARIO)" >> "$SUMMARY_MD"
        echo "" >> "$SUMMARY_MD"
        echo "Grid search results (samples=$SAMPLES, repeats=$REPEATS):" >> "$SUMMARY_MD"
        echo "" >> "$SUMMARY_MD"
        echo "| Parameter | Value |" >> "$SUMMARY_MD"
        echo "|-----------|-------|" >> "$SUMMARY_MD"
        echo "| w_distinct | $BEST_W0 |" >> "$SUMMARY_MD"
        echo "| w_onepair | $BEST_W1 |" >> "$SUMMARY_MD"
        echo "| w_trips | $BEST_W2 |" >> "$SUMMARY_MD"
        echo "| Variance Ratio (STRAT/MC) | $BEST_RATIO |" >> "$SUMMARY_MD"
        echo "| Variance Reduction | $(python3 -c "print(f'{1.0/float(\"$BEST_RATIO\"):.2f}')")× |" >> "$SUMMARY_MD"
        echo "" >> "$SUMMARY_MD"
        echo "Heatmaps:" >> "$SUMMARY_MD"
        echo "- [Fix w_distinct](strat_heatmap_fix_w0.png)" >> "$SUMMARY_MD"
        echo "- [Fix w_onepair](strat_heatmap_fix_w1.png)" >> "$SUMMARY_MD"
        echo "- [Fix w_trips](strat_heatmap_fix_w2.png)" >> "$SUMMARY_MD"
        echo "- [Summary](strat_weights_summary.png)" >> "$SUMMARY_MD"
        echo "" >> "$SUMMARY_MD"

        echo ""
        echo "Appended results to $SUMMARY_MD"
    fi
fi

echo ""
echo "=== Grid-Search Complete ==="
echo "Results: $OUT_CSV"
echo "Heatmaps in: $REPORTS_DIR/"
